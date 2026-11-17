#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ROOT=$(CDPATH= cd -- "$BASE/.." && pwd)
TC=${CPMIMAGE_LLVM_MINGW_BIN:-$ROOT/windows-runtime/llvm-mingw-20261104-ucrt-macos-universal/bin}
ARCH=${CPMIMAGE_WINDOWS_ARCH:-arm64}
case "$ARCH" in
 arm64) TARGET=aarch64; BUILD="$ROOT/windows-runtime/cross-build"; PREFIX="$ROOT/windows-runtime/cross-prefix"; OUTPUT="$ROOT/prototype/build-windows" ;;
 x86-64) TARGET=x86_64; BUILD="$ROOT/windows-runtime/cross-build-x86-64"; PREFIX="$ROOT/windows-runtime/cross-prefix-x86-64"; OUTPUT="$ROOT/prototype/build-windows-x86-64" ;;
 *) echo "Unsupported Windows architecture: $ARCH" >&2; exit 2 ;;
esac
mkdir -p "$BUILD/cpmtools" "$PREFIX" "$OUTPUT"
export CC="$TC/$TARGET-w64-mingw32-clang" AR="$TC/$TARGET-w64-mingw32-ar" RANLIB="$TC/$TARGET-w64-mingw32-ranlib"
if [ ! -f "$BUILD/libdsk-1.4.2/configure" ]; then tar xzf "$ROOT/vendor/libdsk-1.4.2.tar.gz" -C "$BUILD"; fi
# libdsk's bundled config scripts predate ARM64; use cpmtools' newer copies.
if [ ! -f "$BUILD/cpmtools/cpmtools-2.23/configure" ]; then tar xzf "$ROOT/vendor/cpmtools-2.23.tar.gz" -C "$BUILD/cpmtools"; fi
cp "$BUILD/cpmtools/cpmtools-2.23/config.guess" "$BUILD/libdsk-1.4.2/config/config.guess"
cp "$BUILD/cpmtools/cpmtools-2.23/config.sub" "$BUILD/libdsk-1.4.2/config/config.sub"
sh "$BASE/scripts/apply-libdsk-td0-patch.sh" "$BUILD/libdsk-1.4.2"
cd "$BUILD/libdsk-1.4.2"
./configure --build=aarch64-apple-darwin --host="$TARGET-w64-mingw32" --prefix="$PREFIX" --disable-shared --enable-static
make -C lib -j2;make -C include install;make -C lib install
if [ ! -f "$BUILD/cpmtools/cpmtools-2.23/configure" ]; then tar xzf "$ROOT/vendor/cpmtools-2.23.tar.gz" -C "$BUILD/cpmtools"; fi
cd "$BUILD/cpmtools/cpmtools-2.23"
./configure --build=aarch64-apple-darwin --host="$TARGET-w64-mingw32" --with-libdsk="$PREFIX" --with-diskdefs=diskdefs
python3 "$BASE/scripts/apply-cpmtools-patches.py" "$BUILD/cpmtools/cpmtools-2.23"
make cpmfs.o device_libdsk.o -j2
"$CC" -DNOTWINDLL -DCPMIMAGE_READ_ONLY_NAMES '-DDISKDEFS="diskdefs"' -I . -I "$PREFIX/include" -c cpmfs.c -o cpmfs-readonly.o
"$CC" -DCPMIMAGE_READ_ONLY -DCPMIMAGE_TELEDISK -DNOTWINDLL -I . -I "$PREFIX/include" "$ROOT/experiments/process-comparison/helper.c" cpmfs-readonly.o device_libdsk.o -L "$PREFIX/lib" -ldsk -o "$OUTPUT/helper-readonly.exe"
