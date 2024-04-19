#!/bin/sh

sed s/@@VERSION@@/$1/ < $MESON_DIST_ROOT/ostreeinit.spec.in > $MESON_DIST_ROOT/ostreeinit.spec
