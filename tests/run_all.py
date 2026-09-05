"""Run both test layers.

    .venv\\Scripts\\python.exe tests\\run_all.py        both layers
    .venv\\Scripts\\python.exe tests\\run_all.py -u     unit only (fast)
    .venv\\Scripts\\python.exe tests\\run_all.py -v     show each suite's output

Unit tests (pytest, assertion-based) cover the pure logic: container sniffing,
decoder ranking, cache eviction, playlist ordering, colour helpers. They need no
files and no display.

Functional suites drive the real thing end to end -- decoding actual files,
building a real window, sending real drag and key events. They report by
printing rather than asserting, because most of what they establish is better
read than asserted: which decoder won, how long a decode took, what the status
bar ended up saying. This runner therefore judges them on their exit code and on
failure markers in their output.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / ".testwork"
FUNCTIONAL = ROOT / "tests" / "functional"

SUITES = [
    ("formats", "test_formats.py", "13 formats decode, magic bytes beat extensions"),
    ("raw", "test_raw.py", "two-stage RAW, dispatch, cache eviction"),
    ("color", "test_color.py", "ICC to sRGB against real system profiles"),
    ("gui", "test_gui.py", "walk a folder, zoom, stale-result discard"),
    ("drop-routing", "test_drop_routing.py", "which widget receives drops"),
    ("dragdrop", "test_dragdrop.py", "single/multi/folder drops, rejection"),
    ("shortcuts", "test_shortcuts.py", "every navigation and zoom key"),
    ("ui", "test_ui.py", "menus, toolbar, action state, fullscreen, log"),
    ("failures", "test_failures.py", "unreadable files: message, no modals"),
]

FAILURE_MARKERS = [
    re.compile(r"\bTraceback\b"),
    re.compile(r"^\s*failures:\s*\[", re.M),
    re.compile(r"\bLEAKED\b"),
    re.compile(r"\bFAILED\b"),
    re.compile(r"[1-9]\d* failures out of"),
]


def judge(output: str, code: int) -> tuple[bool, str]:
    if code != 0:
        return False, "exit code " + str(code)
    for marker in FAILURE_MARKERS:
        hit = marker.search(output)
        if hit:
            return False, "matched " + hit.group(0).strip()
    return True, ""


def run_unit(verbose: bool) -> bool:
    print("== unit (pytest) ==")
    proc = subprocess.run(
        [sys.executable, "-m", "pytest"] + (["-v"] if verbose else []),
        cwd=ROOT,
        capture_output=not verbose,
        text=True,
    )
    if not verbose:
        tail = (proc.stdout + proc.stderr).strip().splitlines()
        for line in tail[-6:] if proc.returncode else tail[-1:]:
            print("  " + line)
    print()
    return proc.returncode == 0


def run_functional(verbose: bool) -> list[tuple[str, bool, str]]:
    print("== functional ==")
    WORK.mkdir(parents=True, exist_ok=True)

    # test_formats.py writes the ordinary samples, but the synthetic RAW has its
    # own generator and test_raw.py expects it to be there already.
    setup = subprocess.run(
        [sys.executable, str(FUNCTIONAL / "make_synthetic_dng.py"), str(WORK)],
        capture_output=True,
        text=True,
    )
    if setup.returncode != 0:
        print("  SETUP FAILED: could not build the synthetic DNG")
        print(setup.stdout + setup.stderr)
        return [("setup", False, "synthetic DNG")]

    results = []
    for label, script, blurb in SUITES:
        proc = subprocess.run(
            [sys.executable, str(FUNCTIONAL / script), str(WORK)],
            capture_output=True,
            text=True,
        )
        output = proc.stdout + proc.stderr
        ok, why = judge(output, proc.returncode)
        results.append((label, ok, why))
        print("  {}  {:<13} {}".format("PASS" if ok else "FAIL", label, blurb))
        if why:
            print("        reason: " + why)
        if verbose or not ok:
            for line in output.strip().splitlines()[-25:]:
                print("        | " + line)
    return results


def main() -> int:
    verbose = "-v" in sys.argv
    unit_only = "-u" in sys.argv

    unit_ok = run_unit(verbose)
    if unit_only:
        return 0 if unit_ok else 1

    results = run_functional(verbose)
    failed = [label for label, ok, _ in results if not ok]

    print("\nunit: {}   functional: {}/{}".format(
        "pass" if unit_ok else "FAIL", len(results) - len(failed), len(results)))
    if failed:
        print("failed suites: " + ", ".join(failed))
    return 0 if unit_ok and not failed else 1


if __name__ == "__main__":
    raise SystemExit(main())
