#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ROOT=$(CDPATH= cd -- "$BASE/.." && pwd)
TC=${CPMIMAGE_LLVM_MINGW_BIN:-$ROOT/windows-runtime/llvm-mingw-20261104-ucrt-macos-universal/bin}
BUILD="$ROOT/windows-runtime/cross-build";PREFIX="$ROOT/windows-runtime/cross-prefix"
mkdir -p "$BUILD/cpmtools" "$PREFIX" "$ROOT/prototype/build-windows"
export CC="$TC/aarch64-w64-mingw32-clang" AR="$TC/aarch64-w64-mingw32-ar" RANLIB="$TC/aarch64-w64-mingw32-ranlib"
if [ ! -f "$BUILD/libdsk-1.4.2/configure" ]; then tar xzf "$ROOT/vendor/libdsk-1.4.2.tar.gz" -C "$BUILD"; fi
# libdsk's bundled config scripts predate ARM64; use cpmtools' newer copies.
if [ ! -f "$BUILD/cpmtools/cpmtools-2.23/configure" ]; then tar xzf "$ROOT/vendor/cpmtools-2.23.tar.gz" -C "$BUILD/cpmtools"; fi
cp "$BUILD/cpmtools/cpmtools-2.23/config.guess" "$BUILD/libdsk-1.4.2/config/config.guess"
cp "$BUILD/cpmtools/cpmtools-2.23/config.sub" "$BUILD/libdsk-1.4.2/config/config.sub"
cd "$BUILD/libdsk-1.4.2"
./configure --build=aarch64-apple-darwin --host=aarch64-w64-mingw32 --prefix="$PREFIX" --disable-shared --enable-static
make -C lib -j2;make -C include install;make -C lib install
if [ ! -f "$BUILD/cpmtools/cpmtools-2.23/configure" ]; then tar xzf "$ROOT/vendor/cpmtools-2.23.tar.gz" -C "$BUILD/cpmtools"; fi
cd "$BUILD/cpmtools/cpmtools-2.23"
./configure --build=aarch64-apple-darwin --host=aarch64-pc-mingw32 --with-libdsk="$PREFIX" --with-diskdefs=diskdefs
python3 "$BASE/scripts/apply-cpmtools-patches.py" "$BUILD/cpmtools/cpmtools-2.23"
make cpmfs.o device_libdsk.o -j2
"$CC" -DNOTWINDLL -DCPMIMAGE_READ_ONLY_NAMES '-DDISKDEFS="diskdefs"' -I . -I "$PREFIX/include" -c cpmfs.c -o cpmfs-readonly.o
"$CC" -DCPMIMAGE_READ_ONLY -DNOTWINDLL -I . -I "$PREFIX/include" "$ROOT/experiments/process-comparison/helper.c" cpmfs-readonly.o device_libdsk.o -L "$PREFIX/lib" -ldsk -o "$ROOT/prototype/build-windows/helper-readonly.exe"
