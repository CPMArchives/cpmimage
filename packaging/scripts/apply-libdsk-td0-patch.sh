#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$1"
if ! grep -q 'dsk_err_t cpmimage_tele_scan(' lib/drvtele.c; then
    patch -p1 < "$base/source/libdsk-1.4.2-teledisk.patch"
fi
