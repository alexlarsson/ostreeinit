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
      fprintf(kmsg_f, "ostreeinit: " __VA_ARGS__); \
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
    printd("fork_execvp(%s)\n", exe[0]);  \
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


/* remove all files/directories below dirName -- don't cross mountpoints */
/* Closes the fd, via fdopendir */
static int recursive_rm(int dfd, int st_dev) {
  autoclosedir DIR* dir = fdopendir(dfd);
  if (!dir) {
    print("failed to open directory\n");
    return -1;
  }

  while (1) {
    errno = 0;
    struct dirent* d = readdir(dir);
    if (!d) {
      if (errno) {
        print("failed to read directory\n");
        return -1;
      }
      break;
    }

    if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
      continue;

    struct stat sb;
    if (fstatat(dfd, d->d_name, &sb, AT_SYMLINK_NOFOLLOW)) {
      print("stat of %s failed\n", d->d_name);
      return -1;
    }

    /* skip if device is not the same */
    if (sb.st_dev != st_dev)
      continue;

    int isdir = S_ISDIR(sb.st_mode);
    if (isdir) {
      const int cfd = openat(dfd, d->d_name, O_RDONLY);
      if (cfd >= 0)
        recursive_rm(cfd, st_dev); /* it closes cfd too */
    }

    if (unlinkat(dfd, d->d_name, isdir ? AT_REMOVEDIR : 0) < 0) {
      print("failed to unlink %s\n", d->d_name);
      return -1;
    }
  }

  return 0;
}

static int switchroot(const char* newroot) {
  if (chdir(newroot)) {
    print("failed to change directory to %s", newroot);
    return -1;
  }

  autoclose int cfd = open("/", O_RDONLY | O_CLOEXEC);
  if (cfd < 0) {
    print("cannot open %s", "/");
    return -1;
  }

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


  struct stat rb;
  if (fstat(cfd, &rb)) {
    print("stat failed\n");
    return -1;
  }

  recursive_rm(cfd, rb.st_dev);
  cfd = -1; /* The fd is closed by recursive_rm */

  return 0;
}

static void do_move_mount(const char* oldmount, const char *newmount) {
  printd("mount(\"%s\", \"%s\", NULL, MS_MOVE, NULL)\n",
         oldmount, newmount);
  if (mount(oldmount, newmount, NULL, MS_MOVE, NULL) < 0) {
    print("failed to mount moving %s to %s, forcing unmount\n",
          oldmount, newmount);
    exit(1);
  }
}

static void do_unmount(const char* oldmount) {
  if (umount2(oldmount, MNT_DETACH) < 0) {
    print("Failed to unmount %s: %s\n", oldmount, strerror(errno));
    exit(1);
  }
}

static void mount_apifs(const char *type, const char *dst, unsigned long mountflags, const char *options) {
  printd("mount(\"%s\", \"%s\", \"%s\", %ld, %s) %d (%s)\n",
        type, dst, type, mountflags, options, errno, strerror(errno));
  if (mount(type, dst, type,mountflags, options) < 0) {
    print("mount of %s failed: %s\n", type, strerror(errno));
    exit(1);
  }
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

static char *
read_proc_cmdline (void)
{
  autofclose FILE *f = fopen ("/proc/cmdline", "r");
  if (!f) {
    print("Failed to open /proc/cmdline");
    exit(1);
  }

  char *cmdline = NULL;
  size_t len;

  /* Note that /proc/cmdline will not end in a newline, so getline
   * will fail unelss we provide a length.
   */
  if (getline (&cmdline, &len, f) < 0) {
    print("Failed to read /proc/cmdline");
    exit(1);
  }

  /* ... but the length will be the size of the malloc buffer, not
   * strlen().  Fix that.
   */
  len = strlen (cmdline);
  if (cmdline[len - 1] == '\n')
    cmdline[len - 1] = '\0';

  return cmdline;
}

static char *
find_proc_cmdline_key (const char *cmdline, const char *key)
{
  const size_t key_len = strlen (key);
  for (const char *iter = cmdline; iter;) {
    const char *next = strchr (iter, ' ');
    if (strncmp (iter, key, key_len) == 0 && iter[key_len] == '=') {
      const char *start = iter + key_len + 1;
      if (next)
        return strndup (start, next - start);

      return strdup (start);
    }

    if (next)
      next += strspn (next, " ");

    iter = next;
  }

  return NULL;
}

int main(int argc, char* argv[]) {
  (void)argv;

  mount_apifs("devtmpfs", "/dev",MS_NOSUID | MS_STRICTATIME, "mode=0755,size=4m");

  autofclose FILE* kmsg_f_scoped = NULL;
  log_open_kmsg();
  kmsg_f_scoped = kmsg_f;

  mount_apifs("proc", "/proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
  mount_apifs("sysfs", "/sys", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
  mount_apifs("tmpfs", "/run", MS_NOSUID | MS_NODEV, "mode=0755,size=64m");

  if (mkdir("/sysroot", 0755) < 0)
    print("Failed to mkdir sysroot: %s\n", strerror(errno));

  // TODO: Extract source device and fs type from /proc/cmdline
  autofree char *cmdline = read_proc_cmdline();
  autofree char *bootdev = find_proc_cmdline_key (cmdline, "bootdev");
  if (!bootdev) {
    print("Can't find bootdev= kernel commandline argument");
    exit(1);
  }

  autofree char *bootfs=find_proc_cmdline_key (cmdline, "bootfs");
  if (!bootfs) {
    print("Can't find bootfs= kernel commandline argument");
    exit(1);
  }

  if (mount(bootdev, "/sysroot", bootfs, MS_RDONLY, NULL) != 0)
    print("Failed to mount sysroot: %s\n", strerror(errno));

  char *arg[] = { "/usr/lib/ostree/ostree-prepare-root", "/sysroot", NULL};
  fork_execvp(arg);

  // We need to keep /run alive to make /run/ostree-booted survive
  do_move_mount("/run", "/sysroot/run");
  // Other mounts are better redone in systemd in case there are any special requirements
  do_unmount("/dev");
  do_unmount("/proc");
  do_unmount("/sys");

  if (switchroot("/sysroot") < 0) {
    print("Failed to switchroot to /sysroot\n");
    exit(1);
  }

  //execl_single_arg("/bin/bash");

  exec_init();

  return 0;
}
