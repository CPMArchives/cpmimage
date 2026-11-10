#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ROOT=$(CDPATH= cd -- "$BASE/.." && pwd)
BUILD="$BASE/helper-macos"
PREFIX="$BASE/helper-deps-macos"
export MACOSX_DEPLOYMENT_TARGET=14.4
mkdir -p "$BUILD"
cd "$BUILD"
if [ ! -f cpmtools-2.23/configure ]; then tar xzf "$ROOT/vendor/cpmtools-2.23.tar.gz"; fi
if [ ! -f libdsk-1.4.2/configure ]; then
 tar xzf "$ROOT/vendor/libdsk-1.4.2.tar.gz"
 cd libdsk-1.4.2
 patch -p1 < "$BASE/source/libdsk-homebrew-configure-big_sur.diff"
 cp ../cpmtools-2.23/config.guess config/config.guess
 cp ../cpmtools-2.23/config.sub config/config.sub
 cd ..
fi
if [ ! -f "$PREFIX/lib/libdsk.3.dylib" ]; then
 cd libdsk-1.4.2
 CFLAGS='-O2 -mmacosx-version-min=14.4' ./configure --prefix="$PREFIX" --disable-static
 make -C lib -j4
 make -C include install
 make -C lib install
 cd ..
fi
cd cpmtools-2.23
if [ ! -f Makefile ]; then CFLAGS='-O2 -mmacosx-version-min=14.4' ./configure --with-libdsk="$PREFIX" --with-diskdefs=diskdefs; fi
python3 "$BASE/scripts/apply-cpmtools-patches.py" "$BUILD/cpmtools-2.23"
make -j4
cc -mmacosx-version-min=14.4 -DCPMIMAGE_READ_ONLY_NAMES '-DDISKDEFS="diskdefs"' -I . -I "$PREFIX/include" -c cpmfs.c -o cpmfs-readonly.o
cc -mmacosx-version-min=14.4 -DCPMIMAGE_READ_ONLY -I . -I "$PREFIX/include" \
 "$ROOT/experiments/process-comparison/helper.c" cpmfs-readonly.o device_libdsk.o \
 -L "$PREFIX/lib" -ldsk -o "$BASE/cpmimage-helper-macos"
