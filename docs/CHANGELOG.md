# Changelog

What was done, when, and what it turned up. Newest first.

`TASKS.md` says what is left; this says what has been done. Commit hashes link
each entry to the full reasoning in its commit message.

From 1.0.0 entries collect under `## Unreleased` and a release renames the
heading; see DEVELOPMENT.md, "Versions and releases". Before it they are by
date.

---

## Unreleased

### Documents corrected and tidied, 2026-11-17

- A macOS port is in TASKS.md and progress.md, with what it would take:
  chiefly opening from Finder, which hands a running viewer a
  `QFileOpenEvent` rather than starting a second one. Not started.
- `testdata/` is gone. It held only its README, nothing read it, and it
  duplicated `image_example/`, which the tests, the benchmark, `trim.sh` and
  the package check all read; that README now says so.
- Corrected: cpp/README gave the install as 139 MB (142 MB as of 1.0.0);
  tools/README announced two cautions and listed three. The mutation record
  in DEVELOPMENT.md is the full run's alone, without the history of partial
  runs before it, and its CI paragraph sits after both kinds of test it runs.

### One step to update the installed copy

- `tools/update.ps1` pulls, builds, runs the unit and functional suites at
  three scales, packages, installs silently and checks the installed copy,
  stopping at the first failure; the build and tests come before the
  install, so a broken build never replaces a working one. Asked for in
  place of updating online, which would have meant publishing installers --
  and with them the licence obligations TASKS.md lists -- network code in a
  viewer that has none, and signing to make a download safe to run.
- It will not close a running viewer unless told to, keeps a desktop
  shortcut if there is one, and refuses a checkout with uncommitted changes
  unless `-NoPull` asks for them to be built as they are.

### The mutation harness wants each suite to have run

- `mutate.sh` took a suite's exit code as its failure count, so a test
  binary that never ran -- quarantined or refused by an antivirus, which
  makes bash exit 126 or 127 -- would have read as a mutation caught. Each
  suite must now end on its summary line (`N passed, M failed across K
  suites`); without it the mutation is UNFINISHED, shows the last lines of
  each log, and counts with the survivors. A suite that crashed part-way is
  UNFINISHED too: that may be the mutation, but it is not known.
- The first version of the fix read every suite as passing -- a `local` on
  a line of its own resets `$?` -- and the three version mutations
  SURVIVED. Fixed, and run again.

## 1.0.0 — 2026-11-16

The first numbered release: the viewer as it has been in daily use since
2026-11-15, now with a licence, a record of the libraries it carries, its
version written once, and a release process -- which this release is the
first to follow. Everything below, by date, is how it got here.

### A release process

- Versions follow Semantic Versioning from 1.0.0. DEVELOPMENT.md,
  "Versions and releases", says what raises which part, where the version
  is written (once), how this changelog is kept, and the steps of a
  release in order -- the full mutation run and the installer check among
  them, then an annotated tag `vx.y.z` -- and why no binary is published.

### The version written once

- The version was written three times: `project()` in CMakeLists.txt, which
  the `.exe`'s version block and the installer already followed, and a
  string each in `main.cpp` (`--version`, the log) and `window.cpp` (Help >
  About). A bump that found only the first would have shipped a 1.0.0 whose
  About box said 0.1.0. CMake now writes `version.h` from `project()` and
  `LICENSE`, and both files read it.
- Help > About gives the copyright and licence, and where the third-party
  licences are: the path of the installed `THIRD-PARTY-NOTICES.txt`, or, in
  a development build that has none, where it would be.
- Two functional tests -- the real `.exe`'s `--version` output, and the
  About text -- and three mutations writing a value of their own, all
  caught. The existing mutation that wrote the `.exe`'s version as 1,0,0,0
  would have passed once the project reached 1.0.0; it writes 0,0,1,0.

### What the package carries, and under which licences

- The package now carries `LICENSE.txt` and `THIRD-PARTY-NOTICES.txt`,
  written by `cpp/packaging/notices.sh` at the end of `trim.sh` and again
  before every installer. Nothing in it is kept by hand: each DLL is traced
  to the MSYS2 package that installed it, and that package's own record
  supplies its version, licence, project page and licence texts. 46
  packages account for the 69 DLLs; a DLL no package owns stops the script.
- What the list shows: most are permissive (MIT, BSD, zlib, ICU, Apache),
  the main libraries LGPL (Qt, LibRaw, libheif, libde265, GLib, libexif),
  and three GPL -- the x265 and x264 encoders libheif links, and JBIG-KIT
  under libtiff. None of that binds a viewer kept for its author's own use;
  TASKS.md lists what publishing an installer would take first.
- `verify_package.ps1` reads the installed notices back and wants every DLL
  listed. Adding that found the script had never failed: it printed
  `FAILED` and `LOST` and exited 0, and `verify_installer.ps1` judged it by
  exit code. It now exits 1 on any of them, shown by running it on the
  install from before the notices, which it fails for 69 unlisted files.

### Licensed under MIT

- The repository had been public with no licence at all, which reserves
  every right: readable, and usable by nobody. `LICENSE` is now MIT, with
  the copyright line written there and nowhere else. CMake reads it into
  the `.exe`'s version block (`LegalCopyright`, shown in Properties >
  Details), and `build_installer.sh` into the installer's, which had said
  "ZivKao" by hand.
- A unit test reads `LICENSE` at test time and compares it with what the
  built `.exe` carries, so a stale build or a hand-written value shows;
  a mutation writing it by hand again is caught.

### The Python build archived

- The Python build had been frozen since the C++ port overtook it, and was
  uninstalled from the development machine on 2026-11-15. Its code, tests
  and packaging are now off `main`; the tag `python-final` is the tree as it
  last was. The root README describes the C++ build: it had still opened by
  telling the reader to create a Python venv, and listed as not yet built a
  thumbnail strip, slideshow, info panel and rotation the C++ build has had
  for weeks.
- Two scripts outlived it, because committed files the C++ suites read come
  from them: `make_synthetic_dng.py` (for `samples/synthetic.dng`) moved to
  `cpp/tests/fixtures/`, and `assets/make_icon.py` now writes `cpp/icon.ico`
  rather than the deleted package's copy. Both write their files byte for
  byte, as do the three fixture scripts already there; DEVELOPMENT.md lists
  which script writes what. `requirements-dev.txt` is cut down to what they
  need.

## 2026-11-15 — one window per session

- **Every double-click opened another window.** Asked about as soon as the
  viewer could be the default: Explorer starts a program afresh for every
  file opened, and nothing told a second launch that a first was running.
  Now the first launch holds a named mutex (`Local\`, so per session) and a
  hidden message window; a later one hands its files over with
  `WM_COPYDATA` and exits, and the first opens them and comes to the front,
  restored if minimised and maximised if it was. The handover happens
  before the later launch builds a `QApplication`, so it costs no more than
  starting a process, and it uses nothing the package does not already
  carry -- `QLocalServer` would have brought Qt's networking module back
  into a build `trim.sh` took it out of.
- The later launch passes on its right to take the foreground
  (`AllowSetForegroundWindow`). It has that right because the user just
  double-clicked; the first launch, in the background, does not, and
  without it its window would only flash on the taskbar.
- **A selection opened at once** starts one launch per file, all at once.
  Taken one at a time, whichever arrived last would win; they are gathered
  for 150 ms and opened as one playlist, the first launch's own file
  included when they come within two seconds of it starting.
- A launch with an option -- `-v` for a verbose log, `--version` -- is its
  own, and a later launch whose first never answers (5 s) opens its own
  window rather than lose the file. Closing the window gives up the mutex
  at once, so a launch during shutdown is not handed to a window on its
  way out.
- 12 functional tests, 156 now, among them the real `picviewer.exe`
  launched with a relative path, which must hand over the absolute path
  and exit. Every test takes its own key (`PICVIEWER_INSTANCE_KEY`), so
  none hands files to a viewer the user has open. 11 mutations added, 94 in
  all.

## 2026-11-15 — registered the way Windows offers it, and named "Pic Viewer"

Asked whether the install had been checked against Windows itself. It had
not, quite.

- **Open with buried it, and Settings > Default apps did not list it.** The
  installer wrote `Applications\picviewer.exe` with its 24 `SupportedTypes`,
  and every check on those keys passed. Asked directly
  (`SHAssocEnumHandlers`), the shell listed the viewer in the first Open with
  list for none of the 24 formats, only in the full one behind "Choose
  another app", beside Notepad and Word. Now registered as Microsoft
  documents it: a ProgID, `PicViewerCpp.Image`, under each extension's
  `OpenWithProgids`, and `Capabilities` named in `RegisteredApplications`
  for a page in Settings > Default apps. Still all under HKCU. Windows
  still lets only the user make it the default; this makes that one place
  rather than 24 right-clicks.
- **`cpp/tools/assoc_probe.cpp`** asks the shell what it offers for each
  extension, and `tools/verify_installer.ps1` runs it: after an install it
  wants the viewer offered first, once, and under its own name, for all 24;
  after the uninstall, nowhere. Run against the old install it reported
  recommended=0 for every format.
- **One description of an install.** The NSIS installer wrote its own
  registry entries, a second copy of `install.ps1`'s. It now copies the
  files and runs `uninstall.ps1 -Register` from the install directory, and
  the version recorded under Settings > Apps is read from the exe rather
  than written in two places.
- **The uninstaller said "Nothing of Pic Viewer (C++) is left"** and left
  `%LOCALAPPDATA%\PicViewer` -- settings, logs, the thumbnail cache --
  deliberately. It says what it kept and where now. It also clears
  Explorer's cached display name for the exe, and refuses before removing
  anything if the viewer is running, rather than after the shortcuts.
- **Named "Pic Viewer"**, without "(C++)", now that the Python build is gone
  from this machine. Explorer had cached the old name by path, so the
  install clears that cache entry; shortcuts by the old name are removed,
  and a desktop one is replaced under the new name.

## 2026-11-15 — the .exe's own icon

- **The C++ build's shortcuts showed Windows' generic icon.** Noticed on the
  desktop, beside the Python build's shortcut with its aperture. The window
  icon is set at run time from the Qt resources, so the running app always
  looked right and nothing that drove it could have seen this; but the
  `.exe` had no icon resource, and the desktop and Start Menu shortcuts,
  Settings > Apps and "Open with" all read theirs from the file.
  `cpp/picviewer.rc.in` now gives it the same `icon.ico`, all seven frames,
  and a version block taken from `project()`, so Properties > Details and
  Task Manager say "Pic Viewer 0.1.0" rather than nothing.
- A new `executable` unit suite reads the icon and version back out of the
  built file with the Win32 resource API. 209 unit tests. Two mutations
  added, 83 in all: the `.rc` left out of the build, which is how it
  shipped, and a version not taken from `project()`; both caught.
- **The Python build uninstalled from the development machine**, at the
  owner's request; its source stays. Not with its own uninstaller: the two
  builds' "Open with" keys, `Applications\PicViewer.exe` and
  `Applications\picviewer.exe`, are one key because the registry ignores
  case, and it pointed at the C++ build, which installed last. The Python
  uninstaller would have deleted it and taken "Open with" from the C++ build.
  Its shortcuts, its Settings > Apps entry and its folder were removed by
  hand instead, and that key kept. The Python build is frozen, so its
  uninstaller is left as it is; this is recorded in `cpp/README.md`.

## 2026-11-15 — stopping at the ends, and Close

Both asked for after the first days of real use.

- **Stepping stops at the first and last image.** It wrapped round, so
  holding an arrow key ran through a folder and started on it again with
  nothing to say the end had passed. Previous, Next, First and Last now grey
  out at the end they cannot pass, and prefetch no longer decodes the images
  round the corner, which nothing but a wrap would reach. The slideshow still
  loops: it has its own step, not the arrow keys'.
- **Close** (`Ctrl+W`, `Ctrl+F4`) goes back to the empty window. The folder
  goes too -- with only the image gone, an arrow key would bring the next
  one straight back -- and so do the decoded images in the cache and any
  decodes still queued.
- **A decode landing after the window emptied would have been drawn.** Seen
  while adding Close, before it shipped: the placeholder never moved the
  generation on, so a result for the file just closed still matched and
  would have gone up over the empty window. It moves on now wherever the
  placeholder is shown -- deleting a folder's last file while its full
  decode was running, or opening an empty folder, reached the same path.
  A test closes a file in the instant after opening it, and fails with the
  line taken out.
- One orientation test had reached its second image only by the wrap: it
  stepped on from the last file in its folder. It steps back now.
- 20 functional tests added, 144 now, green at 1x, 1.5x and 2x. Eight
  mutations added, 81 in all; they and the two slideshow mutations whose
  anchors moved were run on their own and all ten caught.

## 2026-11-02 — every mutation, in one run

- `cpp/tools/mutate.sh` run in full: all 73 mutations applied and caught,
  and the baseline green again afterwards. Which suite caught which -- unit,
  functional at 1x, functional at 2x -- is the table in `docs/DEVELOPMENT.md`,
  now copied from the run's output rather than written by hand.
- **The antivirus is confirmed.** The `uninstall.exe` the installer's first
  build wrote is in Avira's quarantine as `HEUR/APC.Misc`: a heuristic and
  cloud-reputation verdict on an unsigned executable nobody had seen before,
  not a signature. The entry below said the cause was inferred; it is not any
  more. Nothing changes -- the installer already writes no executable
  uninstaller -- and signing, which would stop the verdict, needs a purchased
  certificate.

## 2026-11-02 — the benchmark, measured honestly

- Rerun after the move to UCRT64 and four new makers. The first run reported
  the C++ build's HEIC 68% slower; with the benchmark's faults fixed it is
  279 ms -- exactly what it was. Nothing in the decoder had changed: libheif
  alone, by path and from mapped memory, measured the same 190 ms either way.
- **Each decode was one run**, so a cold first launch counted as the result.
  Now three runs, the median kept.
- **Each run copied the file afresh.** The first opens of a freshly written
  file pay for the antivirus reading it: the HEIC measured 353-476 ms copied
  anew each time, 255-318 ms from a copy opened a while before. The file is
  now copied once and opened once untimed before the three timed runs.
- **The median was the maximum.** `[int]1.5` is 2 in PowerShell, which
  rounds half to even, so of three sorted runs the largest was kept. The
  startup median used the same expression and was right only because with
  five runs `[int]2.5` is also 2. Both use `Floor` now, and each case prints
  its three runs.
- The figures in `cpp/README.md`, the root README and `progress.md` are
  replaced. The small files' speed-ups there were overstated, from single
  runs at millisecond resolution. The eight RAW makers are 2.3x to 3.5x
  faster in C++.

## 2026-11-02 — a one-file installer

- `cpp/packaging/build_installer.sh` builds `PicViewerCpp-<version>-setup.exe`
  with NSIS, now in the toolchain: 35 MB, against 141 MB installed. It makes
  the same install as `install.ps1` -- directory, shortcuts, "Open with"
  registration, uninstall entry -- so either replaces or removes the other's.
  Per-user, no administrator prompt; `/S` for silent, `/DESKTOP` for a
  desktop shortcut. The version comes from `cpp/CMakeLists.txt`.
- **The first build's uninstaller was taken by the antivirus.** It wrote an
  NSIS `uninstall.exe`; running it was refused with "Access is denied", the
  file could not even have its permissions read, and moments later it was
  gone -- along with the Settings > Apps entry pointing at it, leaving an
  install nothing could remove. Avira and McAfee are installed on this
  machine and neither log names the file, so the cause is inferred, not
  confirmed; it was not worked around. The installer now writes no
  executable uninstaller and records `uninstall.ps1` instead, the script
  `install.ps1` has always left and this machine has run many times.
- `tools/verify_installer.ps1` installs silently, checks every file,
  shortcut and registry entry, runs the installed copy with no toolchain on
  PATH (24 of 24 files decode, eight makers' RAW among them), runs the
  recorded uninstall command, checks everything went and the logs and
  settings stayed, and installs again. Every check passes.
- The package, rebuilt on UCRT64, is 141 MB in 70 files.

## 2026-11-02 — the tools stop emptying the user's log

- **`trim.sh`, `benchmark.ps1`, `sweep.ps1` and `verify_package.ps1` all
  emptied the application log before each run**, so that what they read back
  was theirs. It is the user's own log -- the history a bug report needs --
  and every packaging run, benchmark and package check had been wiping it.
  Found when the session's command classifier refused a diagnostic script
  that did the same thing. Each tool now notes the log's length before it
  starts and reads only what is appended, through `tools/logtail.ps1` for
  the PowerShell ones; a rotation in between is read whole.
- `trim.sh` then reported the freshly gathered package broken, which it was
  not. Its wait stopped at the first `decoded` line of *any* file; with the
  new half-size stage a RAW takes a little longer, a neighbour's prefetch
  finished first, and the RAW was killed mid-decode and counted as failed. It
  now waits for the file under test's own line.

## 2026-11-02 — a half-size stage for small RAW previews

- A RAW whose embedded preview is small now gets a half-size decode between
  the preview and the full one. Measured on the eight makers' files, alone:

  | File | Embedded preview | Half-size stage | Full decode |
  |---|---|---|---|
  | Sony A7 III ARW | 1616x1080 | 314 ms, 3012x2012 | 1083 ms |
  | Panasonic GH7 RW2 | 1920x1440 | 475 ms, 2896x2176 | 1415 ms |
  | the other six | 2560 or more | not needed | -- |

- It runs before the full decode on the same thread, so the final image is
  that much later; a sharp one comes three times sooner. Delivered as a
  preview, so the status bar still says "preview..." and a slideshow does not
  start its count on it. Two mutations, both caught.

## 2026-11-02 — thumbnail strip

- `T` shows the folder as a strip of thumbnails along the bottom. Clicking
  one opens it; the strip marks and scrolls to whatever is on screen; it
  hides with the rest of the chrome in fullscreen.
- Only thumbnails in sight are made. A test opens sixty files in a narrow
  window and requires that no more than twenty are decoded, and a mutation
  confirms that uniform item sizes are what keeps it so: without them the
  view asks every row for its picture to size it.
- Kept on disk under `%LOCALAPPDATA%\PicViewer\thumbnails`, keyed by path,
  modified time and size so an edited photograph is not shown with its old
  thumbnail; a second look at a folder decodes nothing. Pruned to 200 MB at
  start, least recently seen first. Written with `QSaveFile`, so a crash
  cannot leave half a JPEG to be shown as broken forever.
- A RAW's thumbnail comes from its embedded preview; everything else is
  decoded at thumbnail size, which for JPEG happens in the inverse DCT.
  Seven mutations, all caught.

## 2026-11-02 — saving a turn into a JPEG

- `Ctrl+S` (File > Save Orientation to File) writes the turn on screen into
  the JPEG, after asking. Only the EXIF orientation tag changes; the picture
  is not re-compressed. Three paths, one per kind of file: the tag present
  (its two bytes overwritten in place, nothing else touched); EXIF without
  the tag (the block rewritten by libexif, kept from dropping or "fixing"
  anything); no EXIF (a minimal block inserted after JFIF's APP0). The last
  two are built in memory, checked -- the tag reads back, the file decodes
  the right way round, the camera's record is unchanged -- and swapped in
  with `QSaveFile`. Not offered for RAW, HEIC or PNG.
- The functional test records the pixels on screen, saves, lets the viewer
  re-read the file, and requires the same pixels. A unit test tags one JPEG
  with each of the eight orientations and checks Qt decodes each as the
  transform table says.
- The first set of tests could not tell the file's turn and the view's turn
  composed in the wrong order: every case was a pure rotation, and rotations
  commute. A case with a mirror on top of an existing turn was added; it is
  the only one that catches that mutation. Five mutations, all caught.

## 2026-11-02 — position, and EXIF from every container

- The info panel shows a GPS position and altitude, read from EXIF's GPS
  directory or, for RAW, from LibRaw. The position is a link to OpenStreetMap
  that opens only if clicked. 0,0 is treated as no fix, since that is what a
  camera without one writes.
- EXIF is now read from PNG (an `eXIf` chunk), WebP (an `EXIF` chunk) and
  TIFF as well as JPEG and HEIC. A TIFF's EXIF is in the file's own
  directories, which libtiff writes after the pixel data, and libexif reads
  only the first 64 KiB of what it is given. A unit test hands it a whole
  TIFF with 100 KB of pixels first and confirms it finds nothing; the viewer
  copies the entries it uses into a compact block instead.
- Fixtures: PNG and WebP from Pillow; the TIFF written by hand in
  `make_exif.py`, big-endian, directories after 90 KB of pixels, with a
  position south and west. Pillow's own TIFF writer drops the EXIF and GPS
  directories, so it could not make one. The HEIC fixture gained a position.
- **The mutation harness reported a pass with a mutation that did not
  compile.** An INVALID result was printed and then not counted, so the run
  ended "every mutation was applied and caught". It now counts like SKIP: a
  mutation that does not build tested nothing. The one that failed to build
  was rewritten; all eight new mutations are caught.

## 2026-11-02 — animated GIF and WebP

- Animated GIF and WebP play. The first frame comes through the decoders
  like any image, so the cache, the info panel and the status bar treat it
  as usual; then a QMovie plays the frames over it, decoding one at a time
  rather than holding every frame in memory. `P` pauses and resumes. Each
  frame keeps the view, so zoom, pan and a turn hold while it plays.
- The animation is stopped first thing on every move to another image,
  failure or close. Left running, the last file's frames would keep landing
  on the next picture; a test steps from a GIF to a PNG and requires the PNG
  to stay put.
- Fixtures `anim.gif` and `anim.webp`, three solid frames each, a few hundred
  bytes, from `make_anim.py`. The tests read the canvas centre over time and
  require all three colours. Five mutations, all caught.

## 2026-11-02 — slideshow

- `F5` starts a slideshow, fullscreen, looping through the folder. Each
  image's interval runs from when it lands rather than from the step, so a
  RAW that takes a second and a half to decode still gets its full turn; an
  unreadable file is passed over after the interval rather than stopping it.
  `Escape`, `F5` again or `F11` ends it. Started from fullscreen, stopping
  leaves the window fullscreen; started from a window, it goes back to one.
- **Fixed:** leaving fullscreen always called `showNormal()`, so a window
  maximized before `F11` came back un-maximized.
- Writing the mutations showed two redundancies. Escape had its own slideshow
  branch, but leaving fullscreen already ends a slideshow, so the branch
  went. And a single image is refused twice -- the action greys out, and the
  slot checks again -- so the test now checks each on its own; otherwise
  removing either guard would have passed. Six mutations, all caught.

## 2026-11-02 — settings

- File > Settings (`Ctrl+,`): memory cache size, how many images to decode
  ahead on each side, slideshow interval. The cache and prefetch take effect
  at once; lowering the cache frees memory then, not at the next decode.
- The window's size, position and open panels are remembered. Closing while
  fullscreen saves the windowed layout, not the fullscreen one with its
  chrome and panels hidden.
- Stored in `picviewer-cpp.ini` beside the logs, clamped on load. The test
  binaries point at a temporary file so a test run never reads or overwrites
  the real one.
- Prefetch now queues nearest first, alternating forward and back.
- The restore test failed at 1.5x and 2x at first: the offscreen screen is
  800x600 physical, so 400x300 logical at 2x, and a 720x510 window does not
  fit -- Qt shrinks a saved geometry that does not fit the screen, which is
  right. The test now sizes the window to the screen it is on.
- `mutate.sh` takes `ONLY="regex"` to run just the matching mutations; six
  new ones, all caught.
- `.gitattributes` keeps `*.sh` at LF. With `core.autocrlf` on, as is the
  Windows default, a fresh checkout would have given every script CRLF, and
  bash fails on the first line of such a file. Nothing had broken only
  because the working copies were written with LF.

## 2026-11-02 — UCRT64, and four more makers

- **The toolchain moved from MSYS2's MINGW64 environment to UCRT64.** MINGW64
  is being retired: its repository had shrunk to 1,648 packages against
  UCRT64's 3,945, and the CI action flagged it deprecated. `.msys64` now holds
  UCRT64 only; scripts, `env.cmd`, CI and the docs all point there.
- The first UCRT64 build failed with `at_quick_exit` and `timespec_get` not
  declared. With both environments installed, Qt's CMake config found Vulkan
  headers under `mingw64/include`, and that directory's C headers -- written
  for the older runtime -- were then picked up ahead of UCRT64's. Removing the
  102 MINGW64 packages fixed it; nothing can mix the two now.
- Versions: gcc 16.2, Qt 6.12, LibRaw 0.22.3, libheif 1.23.7, libexif 0.6.26.
  All tests pass unchanged.
- Four more CC0 samples from raw.pixls.us, each checked against the site's
  SHA-256: Olympus E-M10 II ORF, Panasonic DC-GH7 RW2 (a 2024 body), Pentax
  K-r PEF, Leica M10-R DNG. All decode. `everyRealRawDecodes` now reads every
  camera file in `image_example/`, eight makers, requiring the preview, the
  RAW decoder, sensor-sized output and the camera's metadata.

## 2026-11-01 — paths outside the code page

- **Found while reading the decoders for the info panel, then confirmed on
  the installed build** by opening copies of the samples under other names.
  On this machine (code page 950, Big5):

  | Path | RAW (DNG) | HEIC |
  |---|---|---|
  | ASCII | decoded | decoded |
  | Traditional Chinese name | decoded | **failed** |
  | ASCII name in a folder called 照片 | decoded | **failed** |
  | Simplified Chinese or emoji name | **200x150 thumbnail, via Qt** | **failed** |

- Cause: both libraries were given `path.toLocal8Bit()`. libheif reads a
  `char*` path as UTF-8, so every non-ASCII character broke it. LibRaw's
  `char*` open goes through the ANSI code page, so only characters outside it
  did -- and then the Qt decoder, next in line, opened the same file and
  returned the thumbnail from its first TIFF directory, with nothing on
  screen to say so.
- Fix: LibRaw is opened through its UTF-16 overload; libheif is given the
  file's bytes, mapped through QFile, and never sees a path.
- `UnicodePathTests`: nine names in two formats, each required to decode
  through the right decoder at full size, and a RAW preview under an emoji
  name. All six that mattered failed before the fix, in exactly the pattern
  above. Two mutations put the old open calls back.
- Every sample, fixture and real file in the project had an ASCII name, which
  is why nothing caught this before.

## 2026-11-01 — info panel

- `I` opens a side panel with what the camera recorded -- body, lens, focal
  length (with the 35 mm equivalent when it differs), aperture, shutter, ISO,
  exposure bias, date taken -- and the file's own details: name, dimensions,
  size, format, decoder, colour profile, modified date. On the View menu and
  the toolbar; closed by default; hidden in fullscreen and put back after.
- RAW metadata from LibRaw, which the RAW decoder already has open. JPEG and
  HEIC/AVIF EXIF through libexif, newly added to the toolchain and to CI.
  exiv2 was the first choice and is no longer built for MINGW64.
- libexif "follows the specification" by default, rewriting and adding
  entries on load; that is turned off. The code first said it invented a
  date for undated photographs. Checked by turning it back on: the fields
  read here come out the same, an undated block included, so the comment now
  says only what is documented and no mutation pretends to guard it.
- Two committed fixtures, `exif_camera.jpg` and `exif_phone.heic`, about 1 KB
  each, from `cpp/tests/fixtures/make_exif.py`; a big-endian EXIF block is
  built by hand in the unit tests, since Pillow writes only little-endian.
  41 new unit tests, 9 new functional; the real CR2 is read where present.
- One new functional test failed at first: `I` pressed straight after
  `show()`, before the offscreen window was active, so the shortcut never
  fired. The test now waits for the window; the panel was fine.

## 2026-11-01 — CI

- `.github/workflows/cpp.yml`: on every push, a clean Windows runner installs
  the toolchain from MSYS2, builds, and runs the unit suite and the functional
  suite at 1x, 1.5x and 2x. About three minutes. The application log is
  uploaded when anything fails.
- **The first run failed, and was right to.** `theLogIsBeingWritten` checked
  only that the log file was not empty. The development machine's log holds
  every earlier session, so it always passed there; on the runner the file
  was empty. Inside `qExec`, QTest installs its own message handler and
  forwards nothing, so no line a test logs reaches the application's log.
  The test now looks for a marker `main()` logs before any suite starts,
  unique to the run. A 26th mutation, which stops the logger flushing each
  line, fails it.
- **Corrected:** the empty file was first put down to Windows not updating a
  file's directory-entry size while it is held open. Reading the file back
  showed the line was simply not there; the QTest handler was the cause.
- The runner flags MINGW64 as deprecated, and MSYS2's MINGW64 repository is
  shrinking: 1,648 packages against UCRT64's 3,945, exiv2 already gone. Moving
  the toolchain to UCRT64 is now in `TASKS.md`.

## 2026-11-01 — rotate and flip

- `R` / `L` turn the picture a quarter right or left, `H` / `V` mirror it.
  On the View menu, and ↺ ↻ on the toolbar. The status bar names the
  orientation ("rotated 90°", "flipped horizontally"), the log records each
  step, and `Ctrl+C` copies the image as shown. The file is never changed.
- The turn is applied to the picture, not to the view. The view's transform
  is the zoom, read as its `m11`, and a quarter turn makes `m11` zero; turning
  the view would have broken the zoom readout, fit, and display scaling at
  once.
- A new image starts upright. The full decode of a RAW replacing its preview
  keeps the turn, since it is the same picture. Zoomed in, the detail under
  the centre stays there through a turn.
- New `OrientationTests` suite (18 tests), reading colours off the rendered
  canvas from a four-quadrant image, at 1x, 1.5x and 2x. Turn-then-flip and
  flip-then-turn have a row each, because they are different pictures.
- One test failed at 2x on first run: a zoomed-in detail drifted 4 image
  pixels through a turn, against a tolerance of 6 pixels summed. That is
  scroll rounding -- one logical pixel is two image pixels at 2x, and centring
  rounds before and after the turn -- so the tolerance now scales with the
  display ratio. Losing the detail is hundreds of pixels.
- Nine mutations added to the harness for the likeliest ways to get this
  wrong -- turning the view instead of the picture, composing in the wrong
  order, clockwise the wrong way, keeping a turn into the next image, and so
  on. All 25 now applied and caught; baseline green before and after.
- `cpp/README.md` no longer claims the functional suites are unported; they
  were ported on 2026-10-24.
- The documented `.msys64\usr\bin\bash -lc "bash cpp/..."` commands failed
  with "No such file or directory" when repackaging. An MSYS2 login shell
  changes to `$HOME` unless `CHERE_INVOKING` is set, and this machine's
  Windows environment defines `HOME` elsewhere. The docs now say to set it,
  and `env.cmd` does.

## 2026-10-25 — display scaling, and RAW from three more makers

- **"100%" was not 1:1 on a scaled display.** A `QGraphicsView` transform is
  in logical pixels; at 200% scaling each image pixel became a 2x2 block of
  screen pixels while the status bar said 100%. Zoom is now measured in screen
  pixels, fit compares against the canvas's physical size, and a move to a
  screen with different scaling recomputes the transform.
- `functional_tests --scale N` runs the whole suite as on a display scaled by
  N. A new high-DPI suite renders a one-pixel checkerboard and reads the
  pixels back: with the fix reverted it reports "20 of 40 neighbouring screen
  pixels alternate". All 56 functional tests pass at 1x, 1.5x and 2x.
- Two of the new tests passed with the fix reverted. They asked
  `scaleFactor()` whether the zoom was 1:1, and its definition had been
  reverted too. Rewritten to measure the transform and ratio directly.
- The mutation harness now runs the functional suite a second time at 2x,
  since a display-scaling defect is a no-op at a ratio of 1. All 16 mutations
  applied and caught; the two scaling ones only by the 2x run.
- The harness then reported a green baseline as broken. It now reads three
  results, and its closing check still read two, leaving "0 0" in one name.
  The results above stand; only the closing check was wrong.
- Downloaded three CC0 samples from raw.pixls.us after confirming the licence
  in the site's own data, each checked against its published SHA-256: Nikon
  Z 6 NEF (lossless compressed), Sony A7 III ARW (compressed), Fujifilm X-T30
  RAF (X-Trans). All decode. NEF and ARW carry TIFF magic bytes like the CR2,
  so the decoder ranking is now confirmed on three makers' real files.
- Fujifilm X-Trans takes 5.5 s to demosaic, about four times a Bayer RAW, and
  Sony's embedded preview is only 1616x1080. Both recorded in `TASKS.md`.
- **Corrected:** X-Trans was first recorded at 7.7 s and a CR2 at 4.3 s. Both
  were measured with the other RAWs in the folder being prefetched alongside,
  competing for the CPU. The benchmark now opens each file from a folder of
  its own; alone they are 5.5 s and 1.3 s.
- The benchmark flagged a JPEG going from 1 ms to 2 ms as "+100%, WORSE". A
  change is now flagged only when it clears both a relative and an absolute
  threshold -- 15% and 5 ms for decodes, 30% and 100 ms for startup.
- The benchmark gave every real RAW the same id, so four makers' files
  overwrote each other and only the last survived. One id per format now.

## 2026-10-24 — C++ functional tests, development docs

- Ported the GUI-level suites to C++: drop routing, drag and drop, navigation,
  shortcuts, window chrome, unreadable-file handling. 48 tests driving a real
  `MainWindow` offscreen. Unlike the Python originals they assert rather than
  print.
- Restructured the build so `picviewer_core` includes the GUI layer, and the
  application and both test binaries link one library. The tests drive the real
  window, not a copy.
- Extended the mutation harness to both test layers, 14 reintroduced defects.
  Its first run caught three defects *in the tests themselves*:
  - the allocation-limit test checked a constant, not that the call used it;
  - the stale-result test passed with the check deleted, by luck of file order;
  - three mutations never applied, because a multi-line anchor cannot match
    inside bash double quotes — and the harness treated that as neutral.
  All three fixed; SKIP now fails the run. Final run: 14 of 14 applied and
  caught — the decoding and cache defects by the unit suite, the window and
  canvas defects only by the functional one.
- Added a committed fixture, a 9000x9000 PNG that is 249 KiB on disk and
  309 MiB decoded, replacing a test that depended on a personal photograph.
- Wrote `docs/DEVELOPMENT.md`, `docs/TASKS.md` and this file.
- Nearly committed a deliberately broken `cache.cpp`: `git add -A` was run
  while the mutation harness had it edited. Caught before commit; the hazard is
  documented.

## 2026-10-24 — measurements kept, a claim corrected — `1d43e04`

- Twenty-five diagnostic scripts were in a session scratch directory. The ones
  that earn their keep moved to `tools/` and `cpp/tools/`.
- `tools/benchmark.ps1` records startup, per-format decode time and installed
  size into `tools/baseline.json`, and reports later runs as deltas.
- **Corrected:** the C++ build does not start twice as fast. An earlier
  measurement compared a cold file cache with a warm one; measured properly
  the two builds start in the same time. The real difference is decoding —
  9x on a small JPEG, 1.7x on a 19 MB CR2.

## 2026-10-24 — C++ packaged; first real camera RAW — `2e68545`

- Packaged: 139 MB, installed per-user, side by side with the Python build.
  Verified by running the installed copy with `PATH` cut back to Windows.
- The packaged build had silently lost TIFF and WebP: plugins are loaded
  dynamically and their libraries were not in the dependency walk.
- 42 MB of AV1/HEVC encoders cannot be removed — `libheif` imports them at
  load time and will not load without them.
- **First real Canon CR2 decoded.** It opens `49 49 2A 00`, plain TIFF, which
  is the ambiguity the decoder ranking was built for — confirmed on a real file.
- A 101 MP JPEG failed: Qt refuses over 256 MiB of pixels by default.

## 2026-10-23 — diagnostics — `8fb5d1d`

- Crash handler for the C++ build, which had none.
- Environment recorded at startup: OS, Qt, LibRaw, libheif, screens, DPI.
- Failures log file size, what the file was taken for, and each decoder's reason.
- `picviewer -v` printed the version and exited, because `addVersionOption()`
  claims `-v`.

## 2026-10-23 — C++ port — `1e51e25`, `a2e563f`

- Isolated MSYS2 toolchain in `.msys64/`, the way `.venv` isolates Python.
- Full port of the decoder architecture, the cache, the loader and the window.
- The unit suite ran, passed and printed nothing: QTest's stdout logger is torn
  down after each `qExec`.
- `.ico` files showed their smallest frame instead of their largest.

## 2026-10-23 — Python build packaged — `93c2319`

- PyInstaller build and per-user installer.
- The first build did not start at all: relative imports in `__main__.py` fail
  when PyInstaller runs it as a top-level script.

## 2026-10-04 — launcher — `74aaeef`

- `run.bat` had never worked from outside the project folder.

## 2026-09-05 — the Python viewer — `a71d07c` … `95b97ee`

- Decoder registry over Pillow, pillow-heif and rawpy; two-stage RAW; ICC and
  EXIF; byte-budgeted cache; asynchronous loading.
- **The defining bug:** TIFF-based RAW files were decoded by Pillow, which
  succeeded by returning the embedded preview. Both decoders scored 100 and a
  tie went to registration order.
- Drag and drop did nothing: the `QGraphicsView` viewport consumed every drop.
- Unit tests added, then proven by reintroducing six defects.
- Application icon drawn.
