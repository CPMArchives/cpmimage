#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PLATFORM=${1:-linux}
# Linux build prerequisites include libxcb-util-dev, libxcb-xinput-dev,
# libxcb-glx0-dev, libxkbcommon-x11-dev and the X11/OpenGL/fontconfig headers.
if [ "$PLATFORM" = linux ]; then export CPMIMAGE_QT_DESKTOP_FLAGS="-DFEATURE_xcb=ON"; else export MACOSX_DEPLOYMENT_TARGET=14.4; export CPMIMAGE_QT_DESKTOP_FLAGS="-DCMAKE_OSX_DEPLOYMENT_TARGET=14.4 -DQT_NO_XCODE_MIN_VERSION_CHECK=ON"; fi
ROOT=$(CDPATH= cd -- "$BASE/.." && pwd)
SRC="$BASE/qt-source/qtbase-everywhere-src-6.12.0"
mkdir -p "$BASE/qt-source"
if [ ! -f "$SRC/CMakeLists.txt" ]; then tar xf "$ROOT/vendor/qtbase-everywhere-src-6.12.0.tar.xz" -C "$BASE/qt-source"; fi
# Qt's upstream fix keeps native accessibility queries from deleting table interfaces.
# Apply only on macOS; the exact pinned 6.12.0 source contains this removed method.
if [ "$PLATFORM" = macos ] && grep -q 'removeElementsFromCache' "$SRC/src/plugins/platforms/cocoa/qcocoaaccessibilityelement.mm"; then
  patch -d "$SRC" -p1 --forward --batch < "$BASE/source/qtbase-6.12.0-cocoa-accessibility.patch"
fi
cmake -S "$SRC" -B "$BASE/qt-$PLATFORM/build" \
 $CPMIMAGE_QT_DESKTOP_FLAGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$BASE/qt-$PLATFORM/install" \
 -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
 -DQT_FEATURE_dbus=OFF -DQT_FEATURE_network=OFF -DQT_FEATURE_sql=OFF \
 -DQT_FEATURE_glib=OFF -DQT_FEATURE_icu=OFF -DQT_FEATURE_openssl=OFF \
 -DQT_FEATURE_system_doubleconversion=OFF -DQT_FEATURE_system_pcre2=OFF \
 -DQT_FEATURE_system_zlib=OFF -DQT_FEATURE_system_freetype=OFF \
 -DQT_FEATURE_system_harfbuzz=OFF -DQT_FEATURE_system_png=OFF \
 -DQT_FEATURE_system_jpeg=OFF -DQT_FEATURE_system_md4c=OFF \
 -DQT_FEATURE_system_libb2=OFF -DQT_FEATURE_zstd=OFF
cmake --build "$BASE/qt-$PLATFORM/build" -j2
cmake --install "$BASE/qt-$PLATFORM/build"
