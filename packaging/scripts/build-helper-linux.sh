#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Uses the qualified native cpmtools/libdsk build configured by prototype/build-linux.sh.
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ROOT=$(CDPATH= cd -- "$BASE/.." && pwd)
BUILD="$ROOT/prototype/build";PREFIX="$BUILD/deps"
python3 "$BASE/scripts/apply-cpmtools-patches.py" "$BUILD/cpmtools-2.23"
make -C "$BUILD/cpmtools-2.23" cpmfs.o device_libdsk.o -j2
cc -DCPMIMAGE_READ_ONLY -I "$BUILD/cpmtools-2.23" -I "$PREFIX/include" \
 "$ROOT/experiments/process-comparison/helper.c" "$BUILD/cpmtools-2.23/cpmfs.o" "$BUILD/cpmtools-2.23/device_libdsk.o" \
 -L "$PREFIX/lib" -Wl,-rpath,"$PREFIX/lib" -ldsk -o "$BUILD/helper-readonly"
