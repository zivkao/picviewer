# image_example

Real-world files used to check the decoders against something a camera or a
phone actually produced. The files themselves are not tracked: they are
personal photographs, and this repository is public.

Drop anything here that should be exercised -- camera RAW, phone HEIC, awkward
JPEGs, anything that misbehaves -- and run the viewer against it:

    cpp\build\picviewer.exe -v image_example

The log says which decoder handled each file and how long it took:

    %LOCALAPPDATA%\PicViewer\logs\picviewer-cpp.log

What else reads this folder:

- the unit suite's `everyRealRawDecodes`, one row per camera RAW here; on CI,
  where there are none, it skips;
- `tools/benchmark.ps1` and `tools/verify_package.ps1`;
- `cpp/packaging/trim.sh`, whose check after each library it removes
  decodes `chef-with-trumpet.heic` from here beside the committed samples.
