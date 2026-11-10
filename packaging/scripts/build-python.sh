#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LABEL=${1:-macos}
if [ "$LABEL" = macos ]; then export MACOSX_DEPLOYMENT_TARGET=14.4; fi
BUILD="$BASE/build-python-$LABEL"
PREFIX="$BASE/python-$LABEL"
mkdir -p "$BUILD"
if [ ! -f "$BUILD/Python-3.13.17/configure" ]; then tar xf "$BASE/downloads/Python-3.13.17.tar.xz" -C "$BUILD"; fi
cd "$BUILD/Python-3.13.17"
cat > Modules/Setup.local <<'EOF'
*disabled*
_ssl _hashlib _sqlite3 _tkinter _gdbm _dbm readline _curses _curses_panel _ctypes _uuid _lzma _bz2 zlib _decimal pyexpat _elementtree
EOF
if [ ! -f Makefile ]; then
 ./configure --prefix="$PREFIX" --enable-shared --without-ensurepip --disable-test-modules --without-static-libpython
fi
make -j4
make install
"$PREFIX/bin/python3.13" -I -c 'import hashlib,json,subprocess,selectors,threading,tempfile; print(hashlib.sha256(b"cpmimage").hexdigest())'
