#!/usr/bin/bash

DEST=$1
OSTREEINIT=$2

set -e

BINS=/usr/lib/ostree/ostree-prepare-root

files=()
declare -A filesDict

run_ldd() {
    ldd $1 | grep "=>" | awk "{ print \$3 }"
    ldd $1 | grep -v "=>" | grep ld-linux | awk "{ print \$1 }"
}

add_file() {
    local file=$1
    if [ "${filesDict[$file]}" != 1 ]; then
        add_parents $file
        filesDict[$file]=1
        files+=($file)
    fi
}

add_parents() {
    local p=$(dirname $1)
    while [ "$p" != "/" ]; do
        add_file $p
        p=$(dirname $p)
    done
}

add_binary() {
    local bin=$1
    add_file $bin
    for dep in $(run_ldd $bin); do
        add_file $dep
    done
}


add_file /proc
add_file /sys
add_file /dev
add_file /run

for bin in $BINS; do
    add_binary $bin
done

D=$(mktemp -d)
cp $OSTREEINIT $D/init

echo init  | cpio -D $D  -H newc -o -O $D/initrd
for file in "${files[@]}"; do
    echo $file;
done | cpio -D /  -L -H newc -o -A -O $D/initrd

gzip -c $D/initrd > $DEST

rm -rf $D
