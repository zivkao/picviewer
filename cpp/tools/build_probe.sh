#!/usr/bin/env bash
set -u
export PATH=/mingw64/bin:$PATH
export PKG_CONFIG_PATH=/mingw64/lib/pkgconfig

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=${TMPDIR:-/tmp}/picviewer-probe
mkdir -p "$OUT"
SAMPLES="/d/Project/Claude_Pic_Viewer/samples"

echo "=== compiling probe ==="
g++ -std=c++20 -O1 -o "$OUT/probe.exe" "$HERE/toolchain_probe.cpp" \
    $(pkg-config --cflags --libs Qt6Gui libraw libheif lcms2) 2>&1 | head -30
status=${PIPESTATUS[0]}

if [ ! -f "$OUT/probe.exe" ]; then
    echo "COMPILE FAILED (exit $status)"
    exit 1
fi
echo "  built $(stat -c %s "$OUT/probe.exe") bytes"

echo
echo "=== running ==="
cd /mingw64/bin && "$OUT/probe.exe" "$SAMPLES"
