#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <sys/wait.h>

#define autofree __attribute__((cleanup(cleanup_free)))
#define autofree_str __attribute__((cleanup(cleanup_free_str)))
#define autoclose __attribute__((cleanup(cleanup_close)))
#define autofclose __attribute__((cleanup(cleanup_fclose)))
#define autoclosedir __attribute__((cleanup(cleanup_closedir)))

static inline void cleanup_free(void* p) {
  free(*(void**)p);
}

static inline void cleanup_close(const int* fd) {
  if (*fd >= 0)
    close(*fd);
}

static inline void cleanup_fclose(FILE** stream) {
  if (*stream)
    fclose(*stream);
}

static inline void cleanup_closedir(DIR** dir) {
  if (*dir)
    closedir(*dir);
}

static FILE* kmsg_f = 0;

#define print(...)                                   \
  do {                                               \
    if (kmsg_f) {                                    \
      fprintf(kmsg_f, "storage-init: " __VA_ARGS__); \
      break;                                         \
    }                                                \
                                                     \
    printf(__VA_ARGS__);                             \
  } while (0)

#if 1
#define DEBUG
#define printd(...)     \
  do {                  \
    print(__VA_ARGS__); \
  } while (0)
#else
#define printd(...)
#endif

#define fork_execvp(exe)                 \
  do {                                                \
    printd("fork_execvp_no_wait(%p)\n", (void*)exe);  \
    const pid_t pid = fork();                         \
    if (pid == -1) {                                  \
      print("fail execvp_no_wait\n");                 \
      break;                                          \
    } else if (pid > 0) {                             \
      printd("forked %d fork_execvp\n", pid); \
      waitpid(pid, 0, 0);                      \
      break;                                          \
    }                                                 \
                                                      \
    execvp(exe[0], exe);                              \
    exit(errno);                                      \
  } while (0)


#define SYSROOT "/sysroot"

static int recursive_rm(const int fd);

static int if_directory(const int dfd,
                        const struct dirent* d,
                        const struct stat* rb,
                        int* isdir) {
  struct stat sb;
  if (fstatat(dfd, d->d_name, &sb, AT_SYMLINK_NOFOLLOW)) {
    print("stat of %s failed\n", d->d_name);
    return 1;
  }

  /* skip if device is not the same */
  if (sb.st_dev != rb->st_dev)
    return 1;

  /* remove subdirectories */
  if (S_ISDIR(sb.st_mode)) {
    autoclose const int cfd = openat(dfd, d->d_name, O_RDONLY);
    if (cfd >= 0)
      recursive_rm(cfd); /* it closes cfd too */

    *isdir = 1;
  }

  return 0;
}

static int for_each_directory(DIR* dir, const int dfd, const struct stat* rb) {
  errno = 0;
  struct dirent* d = readdir(dir);
  if (!d) {
    if (errno) {
      print("failed to read directory\n");
      return -1;
    }

    return 0; /* end of directory */
  }

  if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..") ||
      !strcmp(d->d_name, "initoverlayfs"))
    return 1;

  int isdir = 0;
  if (d->d_type == DT_DIR || d->d_type == DT_UNKNOWN)
    if (if_directory(dfd, d, rb, &isdir))
      return 1;

  if (unlinkat(dfd, d->d_name, isdir ? AT_REMOVEDIR : 0))
    print("failed to unlink %s\n", d->d_name);

  return 1;
}

/* remove all files/directories below dirName -- don't cross mountpoints */
static int recursive_rm(const int fd) {
  autoclosedir DIR* dir = fdopendir(fd);
  if (!dir) {
    print("failed to open directory\n");
    return -1;
  }

  struct stat rb;
  const int dfd = dirfd(dir);
  if (fstat(dfd, &rb)) {
    print("stat failed\n");
    return -1;
  }

  while (1) {
    const int ret = for_each_directory(dir, dfd, &rb);
    if (ret <= 0)
      return ret;
  }

  return 0;
}

static int move_chroot_chdir(const char* newroot) {
  printd("move_chroot_chdir(\"%s\")\n", newroot);
  if (mount(newroot, "/", NULL, MS_MOVE, NULL) < 0) {
    print("failed to mount moving %s to /\n", newroot);
    return -1;
  }

  if (chroot(".")) {
    print("failed to change root\n");
    return -1;
  }

  if (chdir("/")) {
    print("cannot change directory to %s\n", "/");
    return -1;
  }

  return 0;
}

static int switchroot_move(const char* newroot) {
  if (chdir(newroot)) {
    print("failed to change directory to %s", newroot);
    return -1;
  }

  autoclose const int cfd = open("/", O_RDONLY | O_CLOEXEC);
  if (cfd < 0) {
    print("cannot open %s", "/");
    return -1;
  }

  if (move_chroot_chdir(newroot))
    return -1;

  switch (fork()) {
    case 0: /* child */
    {
      struct statfs stfs;
      if (fstatfs(cfd, &stfs) == 0 &&
          (stfs.f_type == RAMFS_MAGIC || stfs.f_type == TMPFS_MAGIC)) {
        recursive_rm(cfd);
      } else
        print("old root filesystem is not an initramfs");

      exit(EXIT_SUCCESS);
    }
    case -1: /* error */
      break;

    default: /* parent */
      return 0;
  }

  return -1;
}

static int stat_oldroot_newroot(const char* newroot,
                                struct stat* newroot_stat,
                                struct stat* oldroot_stat) {
  if (stat("/", oldroot_stat) != 0) {
    print("stat of %s failed\n", "/");
    return -1;
  }

  if (stat(newroot, newroot_stat) != 0) {
    print("stat of %s failed\n", newroot);
    return -1;
  }

  return 0;
}

static int switchroot(const char* newroot, const char** move_mounts, const char** umounts) {
  /*  Don't try to unmount the old "/", there's no way to do it. */
  struct stat newroot_stat, oldroot_stat, sb;

  if (stat_oldroot_newroot(newroot, &newroot_stat, &oldroot_stat))
    return -1;

  for (int i = 0; move_mounts[i] != NULL; ++i) {
    const char* oldmount = move_mounts[i];
    autofree char* newmount;
    if (asprintf(&newmount, "%s%s", newroot, oldmount) < 0) {
      print(
          "asprintf(%p, \"%%s%%s\", \"%s\", \"%s\") MS_NODEV, NULL) %d (%s)\n",
          (void*)newmount, newroot, oldmount, errno, strerror(errno));
      return -1;
    }

    if ((stat(oldmount, &sb) == 0) && sb.st_dev == oldroot_stat.st_dev) {
      /* mount point to move seems to be a normal directory or stat failed */
      continue;
    }

    printd("(stat(\"%s\", %p) == 0) && %lx != %lx)\n", newmount, (void*)&sb,
           sb.st_dev, newroot_stat.st_dev);
    if ((stat(newmount, &sb) != 0) || (sb.st_dev != newroot_stat.st_dev)) {
      /* mount point seems to be mounted already or stat failed */
      umount2(oldmount, MNT_DETACH);
      continue;
    }

    printd("mount(\"%s\", \"%s\", NULL, MS_MOVE, NULL)\n", umounts[i],
           newmount);
    if (mount(oldmount, newmount, NULL, MS_MOVE, NULL) < 0) {
      print("failed to mount moving %s to %s, forcing unmount\n", umounts[i],
            newmount);
      umount2(umounts[i], MNT_FORCE);
    }
  }

  for (int i = 0; umounts[i] != NULL; ++i) {
    const char* oldmount = oldmount;
    umount2(oldmount, MNT_FORCE);
  }

  return switchroot_move(newroot);
}

static int mount_proc_sys_dev_run(void) {
  if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV,
            NULL)) {
    print(
          "mount(\"proc\", \"/proc\", \"proc\", MS_NOSUID | MS_NOEXEC | "
          "MS_NODEV, NULL) %d (%s)\n",
          errno, strerror(errno));
    return errno;
  }

  if (mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL)) {
    print(
        "mount(\"sysfs\", \"/sys\", \"sysfs\", MS_NOSUID | MS_NOEXEC | "
        "MS_NODEV, NULL) %d (%s)\n",
        errno, strerror(errno));
    return errno;
  }

  if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID | MS_STRICTATIME,
            "mode=0755,size=4m")) {
    print(
        "mount(\"devtmpfs\", \"/dev\", \"devtmpfs\", MS_NOSUID | "
        "MS_STRICTATIME, \"mode=0755,size=4m\") %d (%s)\n",
        errno, strerror(errno));
    return errno;
  }

  if (mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV| MS_STRICTATIME,
            "mode=0755,size=64m")) {
    print(
        "mount(\"tmpfs\", \"/run\", \"vtmpfs\", MS_NOSUID | "
        "MS_STRICTATIME, \"mode=0755,size=64m\") %d (%s)\n",
        errno, strerror(errno));
    return errno;
  }

  return 0;
}

static FILE* log_open_kmsg(void) {
  kmsg_f = fopen("/dev/kmsg", "w");
  if (!kmsg_f) {
    print("open(\"/dev/kmsg\", \"w\"), %d = errno\n", errno);
    return kmsg_f;
  }

  setvbuf(kmsg_f, 0, _IOLBF, 0);
  return kmsg_f;
}

static void execl_single_arg(const char* exe) {
  printd("execl_single_arg(\"%s\")\n", exe);
  execl(exe, exe, (char*)NULL);
}

static void exec_init(void) {
  execl_single_arg("/sbin/init");
  execl_single_arg("/etc/init");
  execl_single_arg("/bin/init");
  execl_single_arg("/bin/sh");
}

int main(int argc, char* argv[]) {
  (void)argv;

  mount_proc_sys_dev_run();

  autofclose FILE* kmsg_f_scoped = NULL;
  log_open_kmsg();
  kmsg_f_scoped = kmsg_f;

  if (mkdir("/sysroot", 0755) < 0)
    print("Failed to mkdir sysroot: %s\n", strerror(errno));

  if (mount("/dev/vda3", "/sysroot", "ext4", MS_RDONLY, NULL) != 0)
    print("Failed to mount sysroot: %s\n", strerror(errno));

  char *arg[] = { "/usr/lib/ostree/ostree-prepare-root", "/sysroot", NULL};
  fork_execvp(arg);

  const char* move_mounts[] = { "/run", NULL};
  const char* umounts[] = {"/dev", "/proc", "/sys", NULL};
  if (switchroot("/sysroot", move_mounts, umounts) < 0)
    print("Failed to switchroot\n");

  //execl_single_arg("/bin/bash");

  exec_init();

  return 0;
}
