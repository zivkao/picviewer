# testdata

Drop real-world files here: camera RAW, phone HEIC, anything that misbehaves.
Nothing in this folder is tracked by git.

Run the viewer against it:

    .venv\Scripts\python.exe -m picviewer testdata

Then check what the decoders did:

    %LOCALAPPDATA%\PicViewer\logs\picviewer.log
