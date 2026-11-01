#!/usr/bin/env bash
# Build the one-file installer from the package deploy.sh and trim.sh made.
#
#   .msys64\usr\bin\bash -lc "bash cpp/packaging/build_installer.sh"
#
# Writes cpp/dist/PicViewerCpp-<version>-setup.exe. The version is the one in
# cpp/CMakeLists.txt, so there is one place to change it.
set -eu
export PATH=/ucrt64/bin:$PATH

ROOT=/d/Project/Claude_Pic_Viewer/cpp
DIST=$ROOT/dist/PicViewer
[ -f "$DIST/picviewer.exe" ] || { echo "no package at $DIST; run deploy.sh and trim.sh first"; exit 1; }

VERSION=$(sed -n 's/^project(PicViewer VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
[ -n "$VERSION" ] || { echo "no version found in CMakeLists.txt"; exit 1; }
OUT=$ROOT/dist/PicViewerCpp-$VERSION-setup.exe

# makensis is a native Windows program: it is given Windows paths.
makensis -V2 \
    -DVERSION="$VERSION" \
    -DDIST="$(cygpath -w "$DIST")" \
    -DICON="$(cygpath -w "$ROOT/icon.ico")" \
    -DOUTFILE="$(cygpath -w "$OUT")" \
    -DUNINSTALLER="$(cygpath -w "$ROOT/packaging/install.ps1")" \
    "$(cygpath -w "$ROOT/packaging/installer.nsi")"

printf '  %s, %s MB\n' "$(basename "$OUT")" "$(( $(stat -c %s "$OUT") / 1024 / 1024 ))"
