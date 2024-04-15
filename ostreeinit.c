#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <stdarg.h>
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

#if 1
#define DEBUG
#endif

#define autofree __attribute__ ((cleanup (cleanup_free)))
#define autofree_str __attribute__ ((cleanup (cleanup_free_str)))
#define autoclose __attribute__ ((cleanup (cleanup_close)))
#define autofclose __attribute__ ((cleanup (cleanup_fclose)))
#define autoclosedir __attribute__ ((cleanup (cleanup_closedir)))

static inline void
cleanup_free (void *p)
{
  free (*(void **)p);
}

static inline void
cleanup_close (const int *fd)
{
  if (*fd >= 0)
    close (*fd);
}

static inline void
cleanup_fclose (FILE **stream)
{
  if (*stream)
    fclose (*stream);
}

static inline void
cleanup_closedir (DIR **dir)
{
  if (*dir)
    closedir (*dir);
}

static FILE *kmsg_f = 0;

#define LOG_PREFIX "ostreeinit: "
#define LOG_PREFIX_LEN strlen (LOG_PREFIX)

static void
klogv (char const *format, va_list args)
{
  if (kmsg_f)
    {
      // Don't malloc here, because the alloc failure uses this
      size_t format_len = strlen (format);
      char *prefixed_format = alloca (LOG_PREFIX_LEN + format_len + 1);
      if (prefixed_format != NULL)
        {
          memcpy (prefixed_format, LOG_PREFIX, LOG_PREFIX_LEN);
          memcpy (prefixed_format + LOG_PREFIX_LEN, format, format_len + 1);
          format = prefixed_format;
        }
      vfprintf (kmsg_f, format, args);
    }
  else
    {
      vprintf (format, args);
    }
}

__attribute__ ((__format__ (printf, 1, 2))) static void
klog (const char *format, ...)
{
  va_list args;

  va_start (args, format);
  klogv (format, args);
  va_end (args);
}

__attribute__ ((__format__ (printf, 1, 2))) static void
debug (const char *format, ...)
{
#ifdef DEBUG
  va_list args;
  va_start (args, format);
  klogv (format, args);
  va_end (args);
#endif
}

__attribute__ ((__noreturn__)) __attribute__ ((__format__ (printf, 1, 2))) static void
fatal (const char *format, ...)
{
  va_list args;

  va_start (args, format);
  klogv (format, args);
  va_end (args);

  exit (1);
}

__attribute__ ((__noreturn__)) static void
oom ()
{
  fatal ("Out of memory");
}

static char *
xstrdup (const char *str)
{
  char *dup = strdup (str);
  if (dup == NULL)
    oom ();
  return dup;
}

static void
fork_execvp (char **args)
{
  debug ("fork_execvp(%s)\n", args[0]);
  const pid_t pid = fork ();
  if (pid == -1)
    fatal ("fail execvp_no_wait\n");

  if (pid == 0)
    {
      /* In child */
      execvp (args[0], args);
      exit (errno);
    }

  waitpid (pid, 0, 0);
}

/* remove all files/directories below dirName -- don't cross mountpoints */
/* Closes the fd, via fdopendir */
static int
recursive_rm (int dfd, int st_dev)
{
  int result = 0;
  autoclosedir DIR *dir = fdopendir (dfd);
  if (!dir)
    {
      klog ("failed to open directory\n");
      return -1;
    }

  while (1)
    {
      errno = 0;
      struct dirent *d = readdir (dir);
      if (!d)
        {
          if (errno)
            {
              klog ("failed to read directory\n");
              return -1;
            }
          break;
        }

      if (!strcmp (d->d_name, ".") || !strcmp (d->d_name, ".."))
        continue;

      struct stat sb;
      if (fstatat (dfd, d->d_name, &sb, AT_SYMLINK_NOFOLLOW))
        {
          klog ("stat of %s failed\n", d->d_name);
          result = -1;
          continue;
        }

      /* skip if device is not the same */
      if (sb.st_dev != st_dev)
        continue;

      int isdir = S_ISDIR (sb.st_mode);
      if (isdir)
        {
          const int cfd = openat (dfd, d->d_name, O_RDONLY);
          if (cfd >= 0)
            {
              /* Note: recursive_rm closes cfd */
              if (recursive_rm (cfd, st_dev) < 0)
                result = -1;
            }
          else
            {
              klog ("Failed to open %s\n", d->d_name);
              result = -1;
            }
        }

      if (unlinkat (dfd, d->d_name, isdir ? AT_REMOVEDIR : 0) < 0)
        {
          klog ("failed to unlink %s\n", d->d_name);
          result = -1;
        }
    }

  return result;
}

static void
switchroot (const char *newroot)
{
  if (chdir (newroot))
    fatal ("failed to change directory to %s", newroot);

  autoclose int cfd = open ("/", O_RDONLY | O_CLOEXEC);
  if (cfd < 0)
    fatal ("cannot open %s", "/");

  if (mount (newroot, "/", NULL, MS_MOVE, NULL) < 0)
    fatal ("failed to mount moving %s to /\n", newroot);

  if (chroot ("."))
    fatal ("failed to change root\n");

  if (chdir ("/"))
    fatal ("cannot change directory to %s\n", "/");

  struct stat rb;
  if (fstat (cfd, &rb))
    fatal ("stat failed\n");

  /* We ignore errors here, that just means some leaks, its not fatal */
  recursive_rm (cfd, rb.st_dev);
  cfd = -1; /* The fd is closed by recursive_rm */
}

static void
do_move_mount (const char *oldmount, const char *newmount)
{
  debug ("mount(\"%s\", \"%s\", NULL, MS_MOVE, NULL)\n", oldmount, newmount);
  if (mount (oldmount, newmount, NULL, MS_MOVE, NULL) < 0)
    fatal ("failed to mount moving %s to %s, forcing unmount\n", oldmount, newmount);
}

static void
do_unmount (const char *oldmount)
{
  if (umount2 (oldmount, MNT_DETACH) < 0)
    fatal ("Failed to unmount %s: %s\n", oldmount, strerror (errno));
}

static void
mount_apifs (const char *type, const char *dst, unsigned long mountflags, const char *options)
{
  debug ("mount(\"%s\", \"%s\", \"%s\", %ld, %s) %d (%s)\n", type, dst, type, mountflags, options,
         errno, strerror (errno));
  if (mount (type, dst, type, mountflags, options) < 0)
    fatal ("mount of %s failed: %s\n", type, strerror (errno));
}

static FILE *
log_open_kmsg (void)
{
  kmsg_f = fopen ("/dev/kmsg", "w");
  if (!kmsg_f)
    {
      klog ("open(\"/dev/kmsg\", \"w\"), %d = errno\n", errno);
      return NULL;
    }

  setvbuf (kmsg_f, 0, _IOLBF, 0);
  return kmsg_f;
}

static void
execl_single_arg (const char *exe)
{
  debug ("execl_single_arg(\"%s\")\n", exe);
  execl (exe, exe, (char *)NULL);
}

static void
exec_init (void)
{
  execl_single_arg ("/sbin/init");
  execl_single_arg ("/etc/init");
  execl_single_arg ("/bin/init");
  execl_single_arg ("/bin/sh");
}

static char *
read_proc_cmdline (void)
{
  autofclose FILE *f = fopen ("/proc/cmdline", "r");
  if (!f)
    fatal ("Failed to open /proc/cmdline");

  char *cmdline = NULL;
  size_t len;

  /* Note that /proc/cmdline will not end in a newline, so getline
   * will fail unelss we provide a length.
   */
  if (getline (&cmdline, &len, f) < 0)
    fatal ("Failed to read /proc/cmdline");

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
  for (const char *iter = cmdline; iter;)
    {
      const char *next = strchr (iter, ' ');
      if (strncmp (iter, key, key_len) == 0 && iter[key_len] == '=')
        {
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

int
main (int argc, char *argv[])
{
  (void)argv;

  mount_apifs ("devtmpfs", "/dev", MS_NOSUID | MS_STRICTATIME, "mode=0755,size=4m");

  autofclose FILE *kmsg_f_scoped = NULL;
  log_open_kmsg ();
  kmsg_f_scoped = kmsg_f;

  mount_apifs ("proc", "/proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
  mount_apifs ("sysfs", "/sys", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
  mount_apifs ("tmpfs", "/run", MS_NOSUID | MS_NODEV, "mode=0755,size=64m");

  if (mkdir ("/sysroot", 0755) < 0)
    klog ("Failed to mkdir sysroot: %s\n", strerror (errno));

  autofree char *cmdline = read_proc_cmdline ();
  autofree char *root = find_proc_cmdline_key (cmdline, "ostreeinit.root");
  if (!root)
    fatal ("Can't find ostreeinit.root= kernel commandline argument");

  autofree char *rootfstype = find_proc_cmdline_key (cmdline, "ostreeinit.rootfstype");
  if (!rootfstype)
    {
      klog ("Can't find ostreeinit.rootfstype= kernel commandline argument, assuming ext4");
      rootfstype = xstrdup ("ext4");
    }

  if (mount (root, "/sysroot", rootfstype, MS_RDONLY, NULL) != 0)
    fatal ("Failed to mount %s at sysroot (fs %s): %s\n", root, rootfstype, strerror (errno));

  char *arg[] = { "/usr/lib/ostree/ostree-prepare-root", "/sysroot", NULL };
  fork_execvp (arg);

  // We need to keep /run alive to make /run/ostree-booted survive
  do_move_mount ("/run", "/sysroot/run");
  // Other mounts are better redone in systemd in case there are any special requirements
  do_unmount ("/dev");
  do_unmount ("/proc");
  do_unmount ("/sys");

  switchroot ("/sysroot");

  // execl_single_arg("/bin/bash");

  exec_init ();

  return 0;
}
