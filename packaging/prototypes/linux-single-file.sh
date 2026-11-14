#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
umask 077
offset=$(LC_ALL=C awk '/^__CPMIMAGE_PAYLOAD__$/ {print NR+1; exit}' "$0")
payload() { tail -n "+$offset" "$0"; }
digest=$(payload | sha256sum)
digest=${digest%% *}
root=${CPMIMAGE_CACHE_DIR:-"${XDG_CACHE_HOME:-"$HOME/.cache"}/cpmimage-single-file"}
mkdir -p "$root"
target="$root/$digest"
if [ ! -d "$target" ]; then
    scratch=$(mktemp -d "$root/.extract.XXXXXXXX")
    trap 'rm -rf -- "$scratch"' EXIT
    trap 'exit 1' HUP INT TERM
    payload | tar -xzf - -C "$scratch"
    if ln -sT -- "$scratch" "$target" 2>/dev/null; then
        trap - EXIT HUP INT TERM
    else
        [ -d "$target" ] || exit 1
    fi
fi
exec "$target/CPMImage" "$@"
__CPMIMAGE_PAYLOAD__
