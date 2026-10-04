@echo off
rem Launch Pic Viewer from anywhere.
rem
rem PYTHONPATH rather than "cd /d %~dp0": -m only puts the *current* directory
rem on sys.path, so without help the package is invisible from anywhere but the
rem project folder -- but cd-ing there would break a relative path argument,
rem since it would then resolve against the project instead of the caller.
rem
rem start "" detaches, so double-clicking does not leave a console window open
rem for as long as the viewer runs.

set "PYTHONPATH=%~dp0;%PYTHONPATH%"

if not exist "%~dp0.venv\Scripts\pythonw.exe" (
    echo The virtual environment is missing. Create it with:
    echo     py -3.13 -m venv .venv
    echo     .venv\Scripts\python.exe -m pip install -r requirements.txt
    pause
    exit /b 1
)

start "" "%~dp0.venv\Scripts\pythonw.exe" -m picviewer %*
