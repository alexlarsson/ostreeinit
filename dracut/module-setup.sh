#!/usr/bin/bash

install() {
    inst /usr/lib/ostree/ostree-prepare-root
    inst /usr/lib/ostreeinit/ostreeinit /init

    # Don't install bash
    > "${initdir}/usr/bin/bash"

    # Don't install vi and various deps
    > "${initdir}/usr/libexec/vi"
}

