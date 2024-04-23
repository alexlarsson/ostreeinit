#!/bin/sh

sed s/@@VERSION@@/$1/ < $MESON_DIST_ROOT/autoinit.spec.in > $MESON_DIST_ROOT/autoinit.spec
