<#
.SYNOPSIS
    Bring the installed viewer up to date with this checkout, in one step.

.DESCRIPTION
    Pulls (fast-forward only), builds, runs the unit suite and the functional
    suite at 1x, 1.5x and 2x, packages, builds the installer, installs it
    silently, and checks the installed copy decodes every sample with no
    toolchain on PATH and lists every library it carries.

    Each step must pass before the next starts. A build or test failure
    stops it before anything is installed, so a broken build never replaces
    a working one.

    This is not a release: no mutation run, no uninstall round trip, no
    version bump, no tag. DEVELOPMENT.md, "Versions and releases", is that.
    Nothing goes over the network but `git pull`, and nothing is published.

    About half an hour (34 minutes the first time), most of it trim.sh
    re-verifying the package after each library it tries to remove.

.PARAMETER NoPull
    Build what is checked out, uncommitted changes included, without pulling.

.PARAMETER CloseViewer
    Close a running Pic Viewer rather than stopping. The installer closes it
    regardless, so without this the update refuses to start while it is open.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\update.ps1
    powershell -ExecutionPolicy Bypass -File tools\update.ps1 -NoPull
#>

[CmdletBinding()]
param(
    [switch]$NoPull,
    [switch]$CloseViewer
)

$ErrorActionPreference = "Stop"
$Root      = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Bash      = Join-Path $Root ".msys64\usr\bin\bash.exe"
$Build     = Join-Path $Root "cpp\build"
$Installed = Join-Path $env:LOCALAPPDATA "Programs\PicViewerCpp\picviewer.exe"
$Shortcut  = Join-Path ([Environment]::GetFolderPath("Desktop")) "Pic Viewer.lnk"
$Started   = Get-Date

function Stop-Update([string]$why) {
    Write-Host "`nNOT UPDATED: $why" -ForegroundColor Red
    if (Test-Path $Installed) {
        Write-Host "The installed copy is unchanged, unless the line above says otherwise."
    }
    exit 1
}

function Step([string]$what) { Write-Host "`n== $what" -ForegroundColor Cyan }

# D:\Project\x as MSYS2 spells it, /d/Project/x. Worked out here rather than
# with cygpath inside the command, because Windows PowerShell passes a double
# quote inside an argument to a native program as-is and bash then sees the
# command cut short -- the commands below use none.
function ConvertTo-Msys([string]$path) {
    "/" + $path.Substring(0, 1).ToLower() + $path.Substring(2).Replace('\', '/')
}

# A command in the project's own MSYS2, from the project root. Its output goes
# straight to the screen; the exit code is what is judged.
function Invoke-Toolchain([string]$command) {
    $env:CHERE_INVOKING = "1"
    & $Bash -lc "export PATH=/ucrt64/bin:`$PATH; cd '$(ConvertTo-Msys $Root)' && $command" | Out-Host
    return $LASTEXITCODE
}

# The installer closes a running copy without asking; this asks first.
function Confirm-ViewerClosed {
    $running = @(Get-Process picviewer -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) { return }
    if (-not $CloseViewer) {
        Stop-Update "Pic Viewer is open. Close it, or run again with -CloseViewer."
    }
    $running | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}

if (-not (Test-Path $Bash)) { Stop-Update "no toolchain at $Bash; see cpp\README.md" }
$before = if (Test-Path $Installed) { (Get-Item $Installed).VersionInfo.ProductVersion } else { "not installed" }
Confirm-ViewerClosed

# -- source -------------------------------------------------------------------
if ($NoPull) {
    Step "source: as checked out (-NoPull)"
} else {
    Step "source: pull"
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Stop-Update "git is not on PATH" }
    $dirty = git -C $Root status --porcelain
    if ($dirty) {
        Stop-Update "uncommitted changes in the checkout. Commit them, or run with -NoPull to build them as they are."
    }
    git -C $Root pull --ff-only | Out-Host
    if ($LASTEXITCODE -ne 0) { Stop-Update "git pull --ff-only failed: main has diverged from origin" }
}
$commit = (git -C $Root log -1 --format="%h %s" 2>$null)

# -- build and test -----------------------------------------------------------
Step "build"
$configure = if (Test-Path (Join-Path $Build "build.ninja")) { "" } else {
    "cmake -S cpp -B cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release && "
}
if ((Invoke-Toolchain "${configure}cmake --build cpp/build") -ne 0) { Stop-Update "the build failed" }

# Judged by the summary line each suite ends on as well as by the exit code:
# a binary that never ran (an antivirus quarantining it, say) also exits
# non-zero, and one that crashed exits however it crashed.
$suites = @(
    @{ name = "unit";            cmd = "cpp/build/tests/unit_tests.exe" }
    @{ name = "functional, 1x";   cmd = "cpp/build/tests/functional_tests.exe --scale 1" }
    @{ name = "functional, 1.5x"; cmd = "cpp/build/tests/functional_tests.exe --scale 1.5" }
    @{ name = "functional, 2x";   cmd = "cpp/build/tests/functional_tests.exe --scale 2" }
)
foreach ($suite in $suites) {
    Step "tests: $($suite.name)"
    $out = Join-Path $env:TEMP "picviewer-update-tests.log"
    $code = Invoke-Toolchain "QT_QPA_PLATFORM=offscreen $($suite.cmd) > '$(ConvertTo-Msys $out)' 2>&1"
    $summary = Get-Content $out -ErrorAction SilentlyContinue |
               Where-Object { $_ -match '^\d+ passed, \d+ failed across \d+ suites' } | Select-Object -Last 1
    if ($summary) { Write-Host "  $summary" }
    if ($code -ne 0 -or -not $summary -or $summary -notmatch ' 0 failed ') {
        Get-Content $out -ErrorAction SilentlyContinue | Select-String '^FAIL' | Select-Object -First 10 |
            ForEach-Object { Write-Host "  $_" }
        Stop-Update "$($suite.name) tests did not pass; full output in $out"
    }
}

# -- package and install ------------------------------------------------------
Step "package: deploy, trim, installer"
if ((Invoke-Toolchain "bash cpp/packaging/deploy.sh > /dev/null && bash cpp/packaging/trim.sh | tail -4") -ne 0) {
    Stop-Update "packaging failed"
}
$version = (Get-Item (Join-Path $Root "cpp\dist\PicViewer\picviewer.exe")).VersionInfo.ProductVersion
$setup = Join-Path $Root "cpp\dist\PicViewerCpp-$version-setup.exe"
$builtAt = Get-Date
if ((Invoke-Toolchain "bash cpp/packaging/build_installer.sh") -ne 0) { Stop-Update "the installer did not build" }
if (-not (Test-Path $setup) -or (Get-Item $setup).LastWriteTime -lt $builtAt.AddSeconds(-5)) {
    Stop-Update "no fresh $setup"
}

Step "install $version"
Confirm-ViewerClosed
$arguments = @("/S")
if (Test-Path $Shortcut) { $arguments += "/DESKTOP" }    # keep the shortcut there is
$p = Start-Process $setup -ArgumentList $arguments -PassThru -Wait
if ($p.ExitCode -ne 0) { Stop-Update "the installer exited $($p.ExitCode); the install may be incomplete -- run it again by hand" }

Step "check the installed copy"
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "tools\verify_package.ps1") |
    Where-Object { $_ -match 'decoded with no toolchain|^\s+(OK|LOST|BAD)\s|FAILED|not in the notices' } |
    Out-Host
if ($LASTEXITCODE -ne 0) {
    Stop-Update "the installed copy failed its check. It IS installed; reinstall the last good one from cpp\dist"
}

$after = (Get-Item $Installed).VersionInfo.ProductVersion
$minutes = [math]::Round(((Get-Date) - $Started).TotalMinutes, 1)
Write-Host "`nUPDATED: $before -> $after, from $commit, in $minutes min" -ForegroundColor Green
exit 0
