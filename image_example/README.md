# image_example

Real-world files used to check the decoders against something a camera or a
phone actually produced. The files themselves are not tracked: they are
personal photographs, and this repository is public.

Drop anything here that should be exercised -- camera RAW, phone HEIC, awkward
JPEGs -- and run the viewer against it. The log says which decoder handled it
and how long it took:

    %LOCALAPPDATA%\PicViewer\logs\picviewer-cpp.log

`tools/benchmark.ps1` and `tools/verify_package.ps1` pick up whatever is here.
