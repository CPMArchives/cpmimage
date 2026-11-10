#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/prototype/build"
PREFIX="$BUILD/deps"
mkdir -p "$BUILD" "$PREFIX"
cd "$BUILD"
tar xzf "$ROOT/vendor/cpmtools-2.23.tar.gz"
if [ ! -f "$PREFIX/lib/libdsk.so" ]; then
  tar xzf "$ROOT/vendor/libdsk-1.4.2.tar.gz"
  cd libdsk-1.4.2
  cp ../cpmtools-2.23/config.guess config/config.guess
  cp ../cpmtools-2.23/config.sub config/config.sub
  # Old libdsk assumes glibc's major() macro arrives via sys/types.h.
  CPPFLAGS="-include sys/sysmacros.h" ./configure --prefix="$PREFIX"
  make -C lib -j2
  make -C include install
  make -C lib install
  cd "$BUILD"
fi
if [ ! -f cpmtools-2.23/cpmfs.o ]; then
  tar xzf "$ROOT/vendor/cpmtools-2.23.tar.gz"
  cd cpmtools-2.23
  ./configure --with-libdsk="$PREFIX" --with-diskdefs=diskdefs
  make -j2
  cd "$BUILD"
fi
python3 "$ROOT/packaging/scripts/apply-cpmtools-patches.py" "$BUILD/cpmtools-2.23"
make -C "$BUILD/cpmtools-2.23" cpmfs.o device_libdsk.o -j2
cc -DCPMIMAGE_READ_ONLY_NAMES '-DDISKDEFS="diskdefs"' -I cpmtools-2.23 -I "$PREFIX/include" -c cpmtools-2.23/cpmfs.c -o cpmtools-2.23/cpmfs-readonly.o
cc -DCPMIMAGE_READ_ONLY -I cpmtools-2.23 -I "$PREFIX/include" \
  "$ROOT/experiments/process-comparison/helper.c" cpmtools-2.23/cpmfs-readonly.o \
  cpmtools-2.23/device_libdsk.o -L "$PREFIX/lib" -Wl,-rpath,"$PREFIX/lib" \
  -ldsk -o helper-readonly
cmake -S "$ROOT/prototype" -B "$BUILD/qt" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD/qt" -j2
export CPMIMAGE_ROOT="$ROOT" CPMIMAGE_HELPER="$BUILD/helper-readonly"
python3 "$ROOT/prototype/tests/broker_check.py"
ctest --test-dir "$BUILD/qt" --output-on-failure
