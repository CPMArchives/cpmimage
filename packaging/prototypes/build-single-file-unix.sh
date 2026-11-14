#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 macos|linux PACKAGE OUTPUT" >&2
    exit 2
fi
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$1" in
    macos) test -x "$2/Contents/MacOS/cpmimage-review" ;;
    linux) test -x "$2/CPMImage" ;;
    *) exit 2 ;;
esac
scratch=$(mktemp)
trap 'rm -f -- "$scratch"' EXIT
if [ "$1" = macos ]; then
    tar -czf "$scratch" -s "|^$(basename -- "$2")|CPMImage.app|" -C "$(dirname -- "$2")" "$(basename -- "$2")"
else
    tar -czf "$scratch" -C "$2" .
fi
cat "$base/$1-single-file.sh" "$scratch" > "$3"
chmod 755 "$3"
