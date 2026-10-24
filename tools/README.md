# tools

Things that answer a question about the build rather than being part of it.

Most of these were written to chase a specific problem and then kept, because
each one either proves something a test cannot or found a defect that reading
the code had not. They live here rather than in a scratch directory for the
obvious reason: a scratch directory does not survive the afternoon.

## Measuring

```
powershell -ExecutionPolicy Bypass -File tools\benchmark.ps1
powershell -ExecutionPolicy Bypass -File tools\benchmark.ps1 -Update
```

Startup, decode time per format and installed size for both builds, compared
against `baseline.json` and reported as deltas. `-Update` replaces the
baseline; commit it with whatever change moved the numbers.

Decode times come from the application's own log, not from wall-clock timing
around the process, so they exclude startup and are the figure that actually
moves when a decoder changes.

Two cautions that the numbers themselves will not tell you:

- **Startup is cache-sensitive.** The same build measures 700 ms or 1400 ms
  depending on what Windows has paged in. An earlier comparison in this
  project concluded the C++ build started twice as fast; it had compared a
  cold cache against a warm one, and the two are in fact the same. Treat a
  startup delta under about 30% as noise.
- **Real-world files are not committed.** `image_example/` holds personal
  photographs and is untracked, so `raw_real` and `heif_real` appear only on
  a machine that has them. Their absence is not a regression.

## Checking a package

```
powershell -ExecutionPolicy Bypass -File tools\verify_package.ps1
```

Runs the *installed* copy with `PATH` cut back to Windows itself and decodes
every sample. Anything still being resolved out of the build toolchain fails
here instead of on somebody else's machine. This is what caught the packaged
C++ build silently losing TIFF and WebP: its plugins were present but the
libraries beneath them were not.

## Exercising the real application

```
powershell -ExecutionPolicy Bypass -File tools\sweep.ps1
```

Opens every sample with the real executable and reads the application's own
log back to see which decoder handled each file and how long it took. A viewer
that starts and shows a window proves very little; this is what says the
decoder dispatch is still doing what it should.

## C++ specific

Under `cpp/tools/`:

- `verify_toolchain.sh` -- what the isolated MSYS2 root actually provides.
  Worth running first when a build fails strangely.
- `toolchain_probe.cpp` + `build_probe.sh` -- compiles a single file against
  Qt, LibRaw, libheif and lcms2 and decodes one of each format. Separates "the
  libraries are wrong" from "my build system is wrong", which is worth having
  before a long day of CMake errors.
- `mutate.sh` -- reintroduces each defect this project has fixed and checks
  the suite goes red for it. A green suite says nothing until it can fail.
