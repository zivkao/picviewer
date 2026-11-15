#!/usr/bin/env bash
# Write LICENSE.txt and THIRD-PARTY-NOTICES.txt into the package.
#
#   .msys64\usr\bin\bash -lc "bash cpp/packaging/notices.sh"
#
# trim.sh runs this last and build_installer.sh first, so the package and
# every installer carry a list made from the files they actually ship. Nothing here is kept by hand: each DLL in
# cpp/dist/PicViewer is traced to the MSYS2 package that installed it, and
# that package's own record supplies its version, licence and licence texts.
# A DLL no package owns stops the script, rather than going out unaccounted
# for -- which is also what happens when a new dependency turns up.
set -eu
export PATH=/ucrt64/bin:$PATH

ROOT=/d/Project/Claude_Pic_Viewer/cpp
DIST=$ROOT/dist/PicViewer
DB=/var/lib/pacman/local
PREFIX=mingw-w64-ucrt-x86_64-
OUT=$DIST/THIRD-PARTY-NOTICES.txt

[ -f "$DIST/picviewer.exe" ] || { echo "no package at $DIST; run deploy.sh and trim.sh first"; exit 1; }
VERSION=$(sed -n 's/^project(PicViewer VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")

# Which package installed each DLL under bin/ or Qt's plugin tree. Exact
# paths, not patterns: libstdc++-6.dll's "+" would otherwise be a regex, and
# gcc's own file list mentions that name inside a longer path.
declare -A owner
for d in "$DB"/"$PREFIX"*/; do
    pkg=$(basename "$d")
    while IFS= read -r path; do
        owner[$path]=$pkg
    done < <(grep -E '^ucrt64/(bin|share/qt6/plugins/[^/]+)/[^/]+\.dll$' "$d/files" || true)
done

# The package's files, as the package records them.
declare -A shipped
unowned=()
while IFS= read -r rel; do
    case $rel in
        */*) key=ucrt64/share/qt6/plugins/$rel ;;
        *)   key=ucrt64/bin/$rel ;;
    esac
    pkg=${owner[$key]:-}
    if [ -z "$pkg" ]; then unowned+=("$rel"); continue; fi
    shipped[$pkg]="${shipped[$pkg]:-}$rel "
done < <(cd "$DIST" && find . -iname '*.dll' | sed 's#^\./##' | sort)
if [ ${#unowned[@]} -gt 0 ]; then
    echo "no MSYS2 package owns: ${unowned[*]}"
    exit 1
fi

field() {   # field <package dir> <NAME>: one value per line
    awk -v f="%$2%" '$0 == f { on = 1; next } /^%/ { on = 0 } on && NF' "$DB/$1/desc"
}
licence_files() {   # what the package installs as its licence
    grep -E '^ucrt64/share/licenses/[^/]+/.+[^/]$' "$DB/$1/files" && return
    grep -i -E '^ucrt64/.*/(licen[cs]e|copying|copyright|notice)[^/]*$' "$DB/$1/files" || true
}

packages=$(printf '%s\n' "${!shipped[@]}" | sort)
cp "$ROOT/../LICENSE" "$DIST/LICENSE.txt"

{
    echo "Pic Viewer $VERSION -- third-party components"
    echo
    echo "Pic Viewer's own code is under the MIT licence: LICENSE.txt, beside this"
    echo "file."
    echo
    echo "Every other file here is a library built by the MSYS2 project"
    echo "(https://www.msys2.org) and shipped as MSYS2 built it, unmodified. Each"
    echo "stays under its own licence. Below, every package with its version and"
    echo "licence and the files it accounts for; then the licence texts each"
    echo "package installs. The source of each is at"
    echo "https://repo.msys2.org/mingw/sources/, as mingw-w64-<name>-<version>."
    echo
    echo "=============================================================================="
    for pkg in $packages; do
        name=$(field "$pkg" NAME); name=${name#"$PREFIX"}
        licence=$(field "$pkg" LICENSE | sed 's/spdx://g' | paste -sd ';' - | sed 's/;/; /g')
        echo
        printf '%s %s\n' "$name" "$(field "$pkg" VERSION)"
        printf '    licence: %s\n' "$licence"
        printf '    project: %s\n' "$(field "$pkg" URL)"
        printf '    files:   %s\n' "${shipped[$pkg]% }"
    done
    echo
    echo "=============================================================================="
    echo "Licence texts"
    echo "=============================================================================="
    for pkg in $packages; do
        name=$(field "$pkg" NAME); name=${name#"$PREFIX"}
        texts=$(licence_files "$pkg")
        if [ -z "$texts" ]; then
            echo
            echo "---- $name: the package installs no licence text; see its project page."
            continue
        fi
        for t in $texts; do
            echo
            echo "---- $name: ${t#ucrt64/} ----"
            echo
            tr -d '\r' < "/$t"
        done
    done
} > "$OUT"

printf '  %s packages, %s files, notices %s KB\n' \
    "$(echo "$packages" | wc -l)" \
    "$(cd "$DIST" && find . -iname '*.dll' | wc -l)" \
    "$(( $(stat -c %s "$OUT") / 1024 ))"
