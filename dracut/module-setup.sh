#!/usr/bin/bash

install() {
    inst /usr/lib/ostree/ostree-prepare-root
    inst /usr/lib/autoinit/autoinit /init
}
