#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Run from a fresh, extracted LibDsk 1.4.2 tree. Arguments go to configure.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test -f tools/dskid.c
sh "$base/scripts/apply-libdsk-td0-patch.sh" "$PWD"
if [ "$(uname -s)" = Linux ]; then
 CPPFLAGS="${CPPFLAGS:+$CPPFLAGS }-include sys/sysmacros.h"
 export CPPFLAGS
fi
# Autoconf probes the unsuffixed name. MinGW exposes GetTempFileName via
# the Windows header macro and otherwise falls back to a Unix /tmp path.
for option do
 case "$option" in
  --host=*mingw*) export ac_cv_func_GetTempFileName=yes ;;
 esac
done
./configure --disable-shared --enable-static "$@"
make -C lib
suffix=$(sed -n 's/^EXEEXT = //p' tools/Makefile)
set --
for tool in dsktrans dskform dskid dskdump dskscan dskutil md3serial apriboot; do
 set -- "$@" "$tool$suffix"
done
make -C tools "$@"
