#!/usr/bin/env bash
# Gather everything picviewer.exe needs into cpp/dist/PicViewer.
#
#   .msys64\usr\bin\bash -lc "bash cpp/packaging/deploy.sh"
#
# windeployqt handles Qt's own runtime and plugins, but knows nothing about
# LibRaw, libheif or the codecs underneath them, so the rest of the dependency
# tree is walked here. Walking it rather than listing it by hand matters: the
# codec chain under libheif (de265, dav1d, aom, x265...) is long and changes
# with the distribution's packaging.
set -u

export PATH=/mingw64/bin:$PATH
ROOT=/d/Project/Claude_Pic_Viewer/cpp
BUILD=$ROOT/build
DIST=$ROOT/dist/PicViewer
MINGW=/mingw64/bin

[ -f "$BUILD/picviewer.exe" ] || { echo "no build at $BUILD/picviewer.exe"; exit 1; }

rm -rf "$DIST"
mkdir -p "$DIST"
cp "$BUILD/picviewer.exe" "$DIST/"

echo "=== Qt runtime and plugins (windeployqt) ==="
windeployqt6 --release --no-translations --no-system-d3d-compiler \
             --no-opengl-sw --no-quick-import --no-compiler-runtime \
             "$DIST/picviewer.exe" 2>&1 | grep -viE "^(Adding|Skipping|Direct|Pat)" | head -8

echo
echo "=== walking the rest of the dependency tree ==="
# Breadth-first over objdump output, keeping only DLLs that live in the
# toolchain. Anything in C:\Windows is part of the OS and must not be copied.
#
# The plugins seed the queue alongside the executable. They are loaded
# dynamically, so nothing in picviewer.exe's import table mentions them --
# walking from the executable alone left libtiff and libwebp behind and the
# packaged build silently lost TIFF and WebP support.
declare -A seen
queue=("$DIST/picviewer.exe")
while IFS= read -r plugin; do
    queue+=("$plugin")
done < <(find "$DIST" -mindepth 2 -name "*.dll")
copied=0

while [ ${#queue[@]} -gt 0 ]; do
    current="${queue[0]}"
    queue=("${queue[@]:1}")

    for dll in $(objdump -p "$current" 2>/dev/null | awk '/DLL Name:/ {print $3}'); do
        [ -n "${seen[$dll]:-}" ] && continue
        seen[$dll]=1
        if [ -f "$MINGW/$dll" ]; then
            cp -n "$MINGW/$dll" "$DIST/" 2>/dev/null && copied=$((copied+1))
            queue+=("$DIST/$dll")
        fi
    done
done
echo "  $copied libraries copied from the toolchain"

# No qt.conf: windeployqt lays the plugins out as imageformats/, platforms/
# and so on directly beside the executable, which is where Qt looks by
# default. Pointing Prefix/Plugins at a plugins/ directory that is never
# created stops every plugin loading.

echo
echo "=== result ==="
printf '  %s MB in %s files\n' \
    "$(du -sm "$DIST" | cut -f1)" "$(find "$DIST" -type f | wc -l)"
echo "  plugin directories:"
for d in "$DIST"/*/; do
    [ -d "$d" ] || continue
    printf '    %-18s %s\n' "$(basename "$d")" "$(ls "$d" | tr "\n" " ")"
done
exit 0
