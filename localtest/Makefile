IMAGE=image.qcow2
KERNEL=vmlinuz
OSTREE_DEPLOYMENT=0b257ba185126d152559cb7395bb9dde2bc4a600687264facd600eebe504eca5
KARGS="loglevel=6 console=ttyS0  autoinit.root=/dev/vda3 autoinit.rootfstype=ext4 ostree=/ostree/boot.1/centos/${OSTREE_DEPLOYMENT}/0 autoinit.rw autoinit.debug"

${IMAGE}:
	echo Build an image (e.g. cs9-qemu-developer-ostree.x86_64.qcow2) and save it here as ${IMAGE}
	echo Then extract the ostree deployment id and set OSTREE_DEPLOYMENT in Makefile (or pass on make commandline)
	exit 1

${KERNEL}:
	echo Extract the kernel from ${IMAGE} and save it here as ${KERNEL}
	exit 1

auto-initrd.img: ../build/autoinit autoinit-mkinitrd prepare-root.conf
	./autoinit-mkinitrd --init=../build/autoinit --copy=/usr/lib/ostree/prepare-root.conf:prepare-root.conf auto-initrd.img

run: ${IMAGE} ${KERNEL}  auto-initrd.img
	qemu-system-x86_64 -nographic -kernel ${KERNEL} -initrd auto-initrd.img -enable-kvm -m 2G -cpu host -drive file=${IMAGE},index=0,media=disk,format=qcow2,if=virtio,snapshot=off -append ${KARGS}
