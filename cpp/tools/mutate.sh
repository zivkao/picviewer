#!/usr/bin/env bash
# Reintroduce each defect this project has fixed; the suites must go red.
#
#   .msys64\usr\bin\bash -lc "bash cpp/tools/mutate.sh"
#
# A green suite says nothing until it can fail. Every mutation below is code
# that was actually shipped at some point, and each one is written to keep the
# surrounding code valid, so a failure is the tests objecting rather than the
# compiler refusing.
#
# Two things this harness learned the hard way:
#   - The MSYS2 python is a native Windows build and cannot resolve /d/...
#     paths, so edits handed to it go through cygpath first. Without that the
#     mutations silently never happened and the run proved nothing.
#   - qExec returns the number of failed tests, so an exit code of 2 means
#     "two tests failed" exactly as much as it could mean "build failed". The
#     two need telling apart, hence the sentinel below.
set -u
export PATH=/usr/bin:/mingw64/bin:$PATH
export PKG_CONFIG_PATH=/mingw64/lib/pkgconfig
export QT_QPA_PLATFORM=offscreen

SRC=/d/Project/Claude_Pic_Viewer/cpp/src
BUILD=/d/Project/Claude_Pic_Viewer/cpp/build
BUILD_FAILED=255

build_both() {
    # Windows can hold a lock on a freshly-run test exe, which fails the link
    # for a reason unrelated to the mutation. One retry makes that a non-event.
    if ! cmake --build "$BUILD" > /tmp/build.log 2>&1; then
        sleep 2
        cmake --build "$BUILD" >> /tmp/build.log 2>&1 || return 1
    fi
    return 0
}

# Echoes: <unit failures> <functional failures>, or the sentinel twice.
run_suites() {
    if ! build_both; then
        echo "$BUILD_FAILED $BUILD_FAILED"
        return
    fi
    "$BUILD/tests/unit_tests.exe" > /tmp/unit.log 2>&1
    local unit=$?
    "$BUILD/tests/functional_tests.exe" > /tmp/func.log 2>&1
    local func=$?
    echo "$unit $func"
}

echo "baseline"
read -r base_unit base_func <<< "$(run_suites)"
printf '  unit: %s   functional: %s\n' \
    "$(tail -1 /tmp/unit.log)" "$(tail -1 /tmp/func.log)"
if [ "$base_unit" != "0" ] || [ "$base_func" != "0" ]; then
    echo "  baseline is not green; fix that before trusting anything below"
    tail -6 /tmp/build.log | sed 's/^/    /'
    exit 1
fi

problems=0
printf '\n%-46s %s\n' "MUTATION" "CAUGHT BY"
echo "--------------------------------------------------------------------------"

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
    raise SystemExit('anchor missing')
p.write_text(s.replace(os.environ['FROM'], os.environ['TO'], 1), encoding='utf-8')
" 2>/tmp/py.err
    if [ $? -ne 0 ]; then
        # The edit never happened, so nothing was tested. That is a failure of
        # this run, not a neutral outcome, and is counted with the survivors.
        printf '%-46s SKIP -- anchor not found, nothing tested\n' "$label"
        problems=$((problems + 1))
        cp /tmp/orig.bak "$path"
        return
    fi

    local unit func
    read -r unit func <<< "$(run_suites)"
    cp /tmp/orig.bak "$path"

    if [ "$unit" = "$BUILD_FAILED" ]; then
        printf '%-46s INVALID (build failed)\n' "$label"
        tail -4 /tmp/build.log | sed 's/^/      | /'
        return
    fi

    local by=""
    [ "$unit" != "0" ] && by="unit($unit)"
    [ "$func" != "0" ] && by="${by:+$by }functional($func)"
    if [ -z "$by" ]; then
        printf '%-46s SURVIVED\n' "$label"
        problems=$((problems + 1))
    else
        printf '%-46s %s\n' "$label" "$by"
    fi
}

# -- decoding and dispatch --------------------------------------------------

mutate "Qt decoder: container match alone wins" "decoders/qt_decoder.cpp" \
    "return extensions().contains(ext) ? ScoreExact : ScoreLikely;" \
    "return ext.isNull() ? ScoreLikely : ScoreExact;"

mutate "RAW decoder ranks below Qt on TIFF" "decoders/raw_decoder.cpp" \
    "return tiffExtensions().contains(ext) ? ScoreLikely : ScoreExact;" \
    "return tiffExtensions().contains(ext) ? ScoreLikely : ScoreLikely;"

mutate "sniff ignores magic bytes, trusts the suffix" "sniff.cpp" \
    "const Container found = fromHeader(header);" \
    "const Container found = header.isEmpty() ? Container::Unknown : Container::Unknown;"

mutate "RW2 read as plain TIFF" "sniff.cpp" \
    'if (startsWith(h, "IIU\x00", 4))               return Container::Rw2;' \
    'if (false)                                     return Container::Rw2;'

mutate "ICO takes the first frame, not the largest" "decoders/qt_decoder.cpp" \
    'if (format == "ico" && frames > 1) {' \
    'if (false && format == "ico" && frames > 1) {'

mutate "Qt allocation limit left at its default" "decoders/qt_decoder.cpp" \
    "reader.setAllocationLimit(AllocationLimitMb);" \
    "reader.setAllocationLimit(256);"

# -- cache -------------------------------------------------------------------

mutate "cache evicts by entry count, not bytes" "cache.cpp" \
    "while (m_bytes > m_budget && m_order.size() > 1) {" \
    "while (m_bytes > m_budget * 1000 && m_order.size() > 50) {"

mutate "cache forgets failures immediately" "cache.cpp" \
    "return m_failed.contains(key);" \
    "return m_failed.contains(key) && false;"

# -- the window and the canvas ----------------------------------------------

mutate "drops handled on the window, not the canvas" "viewer.cpp" \
    "    emit filesDropped(paths);" \
    "    Q_UNUSED(paths);"

# Anchors are single lines. One that spans lines never matches: inside bash
# double quotes "\n" is a backslash and an n, not a newline, and three
# mutations once reported SKIP for that reason and tested nothing.

mutate "dragMoveEvent refuses, so drags stall" "viewer.cpp" \
    '    if (m_dragHover) {' \
    '    if (false) {'

# Shown rather than exec()d: a blocking modal would hang the suite instead of
# failing it, and what is being tested is whether one appears at all.
mutate "failures raised as modal dialogs again" "window.cpp" \
    '    qCDebug(logWindow) << "showing failure for" << QFileInfo(path).fileName();' \
    '    { auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Pic Viewer"), message, QMessageBox::Ok, this); box->setWindowModality(Qt::ApplicationModal); box->show(); }'

mutate "fullscreen leaves the chrome on screen" "window.cpp" \
    "    m_toolBar->setVisible(!entering);" \
    "    m_toolBar->setVisible(true);"

mutate "actions never grey out" "window.cpp" \
    "    m_actDelete->setEnabled(hasFile);" \
    "    m_actDelete->setEnabled(true);"

mutate "stale decodes allowed on screen" "window.cpp" \
    '        return;     // the user moved on while this was decoding' \
    '        {}          // the user moved on while this was decoding'

echo
echo "restoring and re-checking baseline"
read -r unit func <<< "$(run_suites)"
printf '  unit: %s   functional: %s\n' \
    "$(tail -1 /tmp/unit.log)" "$(tail -1 /tmp/func.log)"
if [ "$unit" != "0" ] || [ "$func" != "0" ]; then
    echo "  BASELINE BROKEN -- a mutation was not restored; check git diff cpp/src"
    exit 2
fi
echo "  baseline green again"

echo
if [ "$problems" -gt 0 ]; then
    echo "$problems mutation(s) survived or were never applied"
    exit 1
fi
echo "every mutation was applied and caught"
