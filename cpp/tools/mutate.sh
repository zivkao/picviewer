#!/usr/bin/env bash
# Reintroduce each defect the Python version earned its fixes for; the C++
# suite must go red.
#
# The mingw64 python is a native Windows build and cannot resolve /d/... msys
# paths, so every path handed to it goes through cygpath first. Without that
# the edits silently never happened and the run proved nothing.
export PATH=/usr/bin:/mingw64/bin:$PATH
export PKG_CONFIG_PATH=/mingw64/lib/pkgconfig
export QT_QPA_PLATFORM=offscreen

SRC=/d/Project/Claude_Pic_Viewer/cpp/src
BUILD=/d/Project/Claude_Pic_Viewer/cpp/build

run_suite() {
    # Windows can still hold a lock on the freshly-run test exe, which makes
    # the link fail for a reason that has nothing to do with the mutation.
    # One retry turns that into a non-event instead of a false "INVALID".
    if ! cmake --build "$BUILD" --target unit_tests > /tmp/build.log 2>&1; then
        sleep 2
        if ! cmake --build "$BUILD" --target unit_tests >> /tmp/build.log 2>&1; then
            echo 255; return
        fi
    fi
    "$BUILD/tests/unit_tests.exe" > /tmp/run.log 2>&1
    echo $?
}

echo "baseline"
code=$(run_suite)
tail -1 /tmp/run.log | sed 's/^/  /'
[ "$code" != "0" ] && { echo "  baseline not green, aborting"; cat /tmp/build.log | tail -5; exit 1; }
echo

printf '%-46s %s\n' "MUTATION" "RESULT"
printf '%s\n' "--------------------------------------------------------------------------"

mutate() {
    local label="$1" file="$2" from="$3" to="$4"
    local path="$SRC/$file"
    local winpath
    winpath=$(cygpath -w "$path")

    cp "$path" /tmp/orig.bak

    FROM="$from" TO="$to" WP="$winpath" python -c "
import os, pathlib
p = pathlib.Path(os.environ['WP'])
s = p.read_text(encoding='utf-8')
if os.environ['FROM'] not in s:
    raise SystemExit('ANCHOR_MISSING')
p.write_text(s.replace(os.environ['FROM'], os.environ['TO'], 1), encoding='utf-8')
" 2>/tmp/py.err
    if [ $? -ne 0 ]; then
        printf '%-46s SKIP (%s)\n' "$label" "$(tr -d '\n' < /tmp/py.err | tail -c 40)"
        cp /tmp/orig.bak "$path"
        return
    fi

    local code
    code=$(run_suite)
    cp /tmp/orig.bak "$path"

    case "$code" in
        0) printf '%-46s SURVIVED\n' "$label" ;;
        255) printf '%-46s INVALID (build failed)\n' "$label"

           tail -6 /tmp/build.log | sed 's/^/      | /' ;;
        *) printf '%-46s caught  %s\n' "$label" \
               "$(grep -h 'failed' /tmp/run.log | tail -1 | tr -d '\r')" ;;
    esac
}

# Each replacement keeps the surrounding code valid, so a failure is the tests
# objecting rather than the compiler.
mutate "Qt decoder: container match alone wins" "decoders/qt_decoder.cpp" \
    "return extensions().contains(ext) ? ScoreExact : ScoreLikely;" \
    "return ext.isNull() ? ScoreLikely : ScoreExact;"

mutate "RAW decoder ranks below Qt on TIFF" "decoders/raw_decoder.cpp" \
    "return tiffExtensions().contains(ext) ? ScoreLikely : ScoreExact;" \
    "return tiffExtensions().contains(ext) ? ScoreLikely : ScoreLikely;"

mutate "cache evicts by entry count, not bytes" "cache.cpp" \
    "while (m_bytes > m_budget && m_order.size() > 1) {" \
    "while (m_bytes > m_budget * 1000 && m_order.size() > 50) {"

mutate "cache forgets failures immediately" "cache.cpp" \
    "return m_failed.contains(key);" \
    "return m_failed.contains(key) && false;"

mutate "sniff ignores magic bytes, trusts the suffix" "sniff.cpp" \
    "const Container found = fromHeader(header);" \
    "const Container found = header.isEmpty() ? Container::Unknown : Container::Unknown;"

mutate "RW2 read as plain TIFF" "sniff.cpp" \
    'if (startsWith(h, "IIU\x00", 4))               return Container::Rw2;' \
    'if (false)                                     return Container::Rw2;'

mutate "ICO takes the first frame, not the largest" "decoders/qt_decoder.cpp" \
    'if (format == "ico" && frames > 1) {' \
    'if (false && format == "ico" && frames > 1) {'

echo
echo "restoring and re-checking baseline"
code=$(run_suite)
tail -1 /tmp/run.log | sed 's/^/  /'
[ "$code" = "0" ] && echo "  baseline green again" || echo "  BASELINE BROKEN"
