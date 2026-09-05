"""Run every suite and summarise.

    .venv\\Scripts\\python.exe tests\\run_all.py

Each suite prints its own findings; this reports which ones passed. A suite
fails if it exits non-zero or prints one of the failure markers below -- the
suites are assertion-light on purpose, since most of what they check is better
read than asserted (which decoder won, how long a decode took).
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / ".testwork"

# Ordered: samples must exist before anything reads them.
SUITES = [
    ("formats", "test_formats.py", "13 formats decode, magic bytes beat extensions"),
    ("raw", "test_raw.py", "two-stage RAW, dispatch, cache eviction"),
    ("color", "test_color.py", "ICC to sRGB, EXIF orientation"),
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
    re.compile(r"\bFAIL\b"),
    re.compile(r"failures out of", re.M),
]

# "0 failures out of 13" is a pass; only a non-zero count is a failure.
ZERO_FAILURES = re.compile(r"\b0 failures out of\b")


def judge(output: str, code: int) -> tuple[bool, str]:
    if code != 0:
        return False, "exit code " + str(code)
    for marker in FAILURE_MARKERS:
        hit = marker.search(output)
        if not hit:
            continue
        if marker.pattern == "failures out of" and ZERO_FAILURES.search(output):
            continue
        if hit.group(0) == "FAIL" and "FAILED" not in output and "FAIL:" not in output:
            continue
        return False, "matched " + hit.group(0).strip()
    return True, ""


def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    verbose = "-v" in sys.argv

    print("running {} suites, work dir {}\n".format(len(SUITES), WORK))

    # test_formats.py writes the ordinary samples, but the synthetic RAW has its
    # own generator and test_raw.py expects it to be there already.
    setup = subprocess.run(
        [sys.executable, str(ROOT / "tests" / "make_synthetic_dng.py"), str(WORK)],
        capture_output=True,
        text=True,
    )
    if setup.returncode != 0:
        print("  SETUP FAILED: could not build the synthetic DNG")
        print(setup.stdout + setup.stderr)
        return 1

    results = []
    for label, script, blurb in SUITES:
        proc = subprocess.run(
            [sys.executable, str(ROOT / "tests" / script), str(WORK)],
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

    failed = [label for label, ok, _ in results if not ok]
    print("\n{}/{} suites passed".format(len(results) - len(failed), len(results)))
    if failed:
        print("failed: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
