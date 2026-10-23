@echo off
rem Put the project's own toolchain on PATH for this shell, and nothing else.
rem
rem The venv equivalent: .msys64 is a complete, self-contained MSYS2 root that
rem lives inside the project. It does not touch the system PATH, the registry,
rem or any MSYS2 installed elsewhere -- delete the folder and it is gone.
rem
rem   cpp\env.cmd             open a shell with the toolchain available
rem   cpp\env.cmd build       configure and build
rem   cpp\env.cmd test        build and run the unit tests

set "MSYS_ROOT=%~dp0..\.msys64"
set "PATH=%MSYS_ROOT%\mingw64\bin;%MSYS_ROOT%\usr\bin;%PATH%"
set "PKG_CONFIG_PATH=%MSYS_ROOT%\mingw64\lib\pkgconfig"

if not exist "%MSYS_ROOT%\mingw64\bin\g++.exe" (
    echo The toolchain is missing from %MSYS_ROOT%.
    echo See cpp\README.md for how to recreate it.
    exit /b 1
)

if "%1"=="build" (
    cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
    cmake --build "%~dp0build" || exit /b 1
    exit /b 0
)
if "%1"=="test" (
    cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
    cmake --build "%~dp0build" --target unit_tests || exit /b 1
    "%~dp0build\tests\unit_tests.exe"
    exit /b %ERRORLEVEL%
)

echo Toolchain ready: g++, cmake, ninja, Qt6, LibRaw, libheif.
cmd /k
