#!/usr/bin/env bash
# Report what the isolated toolchain actually provides.
export PATH=/mingw64/bin:$PATH
export PKG_CONFIG_PATH=/mingw64/lib/pkgconfig

show() { printf '  %-22s %s\n' "$1" "$($2 2>&1 | head -1)"; }

echo "=== compilers and build tools ==="
show "gcc"        "gcc -dumpfullversion"
show "g++"        "g++ -dumpfullversion"
show "cmake"      "cmake --version"
show "ninja"      "ninja --version"
show "pkg-config" "pkg-config --version"

echo
echo "=== libraries (via pkg-config) ==="
for lib in Qt6Core Qt6Gui Qt6Widgets libraw libheif lcms2; do
    printf '  %-22s ' "$lib"
    pkg-config --modversion "$lib" 2>/dev/null || echo "NOT FOUND"
done

echo
echo "=== Qt image formats Qt itself can read ==="
ls /mingw64/share/qt6/plugins/imageformats/ 2>/dev/null | tr '\n' ' ' | sed 's/^/  /'
echo

echo
echo "=== headers present? ==="
for h in /mingw64/include/qt6/QtWidgets/QApplication \
         /mingw64/include/libraw/libraw.h \
         /mingw64/include/libheif/heif.h \
         /mingw64/include/lcms2.h; do
    [ -f "$h" ] && echo "  OK   $h" || echo "  MISS $h"
done
