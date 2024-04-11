all: ostreeinit ostree-initrd.img

IMAGE=image.qcow2
KERNEL=vmlinuz
OSTREE_DEPLOYMENT=0b257ba185126d152559cb7395bb9dde2bc4a600687264facd600eebe504eca5

ostreeinit: init.c
	gcc -O2 -Wall init.c -o ostreeinit

ostree-initrd.img: init ostreeinit_mkinitrd.sh
	./ostreeinit_mkinitrd.sh ostree-initrd.img ostreeinit

clean:
	rm -f ostreeinit ostree-initrd.img

${IMAGE}:
	echo Build an image (e.g. cs9-qemu-developer-ostree.x86_64.qcow2) and save it here as ${IMAGE}
	echo Then extract the ostree deployment id and set OSTREE_DEPLOYMENT in Makefile (or pass on make commandline)
	exit 1

${KERNEL}:
	echo Extract the kernel from ${IMAGE} and save it here as ${KERNEL}
	exit 1

KARGS="loglevel=6 console=ttyS0  bootdev=/dev/vda3 bootfs=ext4 ostree=/ostree/boot.1/centos/${OSTREE_DEPLOYMENT}/0"
run: ${IMAGE} ${KERNEL} ostreeinit ostree-initrd.img
	qemu-system-x86_64 -nographic -kernel ${KERNEL} -initrd ostree-initrd.img -enable-kvm -m 2G -cpu host -drive file=${IMAGE},index=0,media=disk,format=qcow2,if=virtio,snapshot=off -append ${KARGS}

clang-format:
	git ls-files | grep -Ee "\\.[hc]" | xargs clang-format -style=file -i
