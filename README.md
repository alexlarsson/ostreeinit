# ostreeinit

ostreeinit is an experimental minimal initrd for ostree use. It builds
a single-executable init process with the only task of mounting the
sysroot, running ostree-prepare-root to mount the ostree root and then
switchroot into that and boot it.

The advantage of this over a more complicated initrd (such as one from
regular dracut) is that it is smaller and does less, so the total time
until the full systemd is running in the real rootfs is shorter.

A disadvantage is that the minimal initrd is very inflexible, so it
cannot be amended by the user, which means there is no way to run
custom code earlier than after the main systemd starts.

## Usage

The expected usage is to install ostree and ostreeinit in the build
environment, and then run `dracut -m ostreeinit` (i.e. dracut with
only the ostreeinit module) to produce an initrd file. This can then
be used to boot the system by specifying the required arguments on the
kernel command line.

The supported kernel args are:
 * ostreeinit.root= - The device node to mount the rootfs from (e.g. root=/dev/vda3). This is required.
 * ostreeinit.rootfstype= - The filesystem type used in the mount, if unspecified `rootfs=ext4` is used.
 * ostreeinit.rw - If this is set, /sysroot is mounted read-write.

In addition, ostree-prepare-root needs the standard `ostree=` argument
to be set, so that ostree can tell what deploy to boot.

## Testing

For fast iteration, the localtest/Makefile file contains some targets that
make it easy to test-boot an image.

In the localtest directory, put an ostree-based vm image as
`image.qcow2`, and extract the kernel used as `vmlinux` and the
deployment id used. Then you can test ostreeinit like so:

```
$ mkdir -p build
$ cd build
$ meson ..
$ ninja
$ cd ../localtest
$ make run OSTREE_DEPLOYMENT=0b257ba185126d152559cb7395bb9dde2bc4a600687264facd600eebe504eca5
```
