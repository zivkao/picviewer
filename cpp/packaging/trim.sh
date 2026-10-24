#!/usr/bin/env bash
# Remove what a viewer cannot need, verifying after every step.
#
#   .msys64\usr\bin\bash -lc "bash cpp/packaging/trim.sh"
#
# Guessing is not good enough here: libheif imports its codec libraries at load
# time whether or not the encode paths are ever reached, so a removal that
# looks obviously safe can stop the whole library loading. Each candidate is
# moved aside, the result is exercised against real files, and anything that
# breaks goes straight back.
set -u
export PATH=/mingw64/bin:$PATH

DIST=/d/Project/Claude_Pic_Viewer/cpp/dist/PicViewer
HELD=/tmp/picviewer-held
SAMPLES=/d/Project/Claude_Pic_Viewer/samples
REAL=/d/Project/Claude_Pic_Viewer/image_example

# LOCALAPPDATA is not exported into an msys login shell; USERPROFILE is.
LOG="$(cygpath -u "${USERPROFILE}")/AppData/Local/PicViewer/logs/picviewer-cpp.log"
mkdir -p "$(dirname "$LOG")"
mkdir -p "$HELD"

FILES=(
    "$SAMPLES/sample.jpg"
    "$SAMPLES/sample.png"
    "$SAMPLES/sample.tif"
    "$SAMPLES/sample.avif"
    "$SAMPLES/synthetic.dng"
    "$REAL/chef-with-trumpet.heic"
)

# Exercise every decoder from a PATH with no toolchain on it, so a library the
# package is missing fails here instead of quietly resolving to the build
# environment and passing.
verify() {
    local failures=0
    for f in "${FILES[@]}"; do
        [ -f "$f" ] || continue
        : > "$LOG"
        PATH=/c/Windows/System32:/c/Windows "$DIST/picviewer.exe" "$(cygpath -w "$f")" &
        local pid=$!
        local waited=0
        while [ $waited -lt 50 ]; do
            sleep 0.4
            waited=$((waited + 1))
            grep -q "decoded\|failed\|FATAL" "$LOG" 2>/dev/null && break
        done
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
        grep -q "decoded $(basename "$f")" "$LOG" 2>/dev/null || failures=$((failures + 1))
    done
    echo "$failures"
}

echo "=== baseline: does the gathered build work on its own? ==="
base=$(verify)
echo "  $base of ${#FILES[@]} files failed"
if [ "$base" != "0" ]; then
    echo "  the gathered build is already broken; fix that before trimming"
    tail -5 "$LOG" 2>/dev/null | sed 's/^/    /'
    exit 1
fi

printf '\n%-24s %10s  %s\n' "CANDIDATE" "SIZE" "RESULT"
echo "------------------------------------------------------------"

try_remove() {
    local name="$1"
    local path="$DIST/$name"
    if [ ! -e "$path" ]; then
        printf '%-24s %10s  not present\n' "$name" "-"
        return
    fi

    local size
    size=$(du -sk "$path" | cut -f1)
    rm -rf "${HELD:?}/$name"
    mv "$path" "$HELD/"

    local failed
    failed=$(verify)
    if [ "$failed" = "0" ]; then
        printf '%-24s %7s KB  removed\n' "$name" "$size"
    else
        mv "$HELD/$name" "$path"
        printf '%-24s %7s KB  KEPT, %s file(s) stopped decoding\n' "$name" "$size" "$failed"
    fi
}

# Encoders. This program only ever decodes.
try_remove libx265-217.dll
try_remove libSvtAv1Enc-4.dll
try_remove librav1e.dll
try_remove libaom.dll

# A viewer opens local files and makes no network calls.
try_remove Qt6Network.dll
try_remove tls
try_remove networkinformation
try_remove libcryptopp.dll

echo
printf '=== final: %s MB in %s files ===\n' \
    "$(du -sm "$DIST" | cut -f1)" "$(find "$DIST" -type f | wc -l)"
