<#
.SYNOPSIS
    Install with the one-file installer, check everything it should have put
    in place, uninstall, check everything is gone, and install again.

.DESCRIPTION
    An installer is the one piece of the build nobody exercises until it is on
    someone else's machine. This runs it silently, end to end, against the
    real per-user locations -- so it replaces whatever C++ build is installed,
    and leaves the new one installed, with a desktop shortcut, when it passes.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\verify_installer.ps1
#>

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Setup = Get-ChildItem (Join-Path $Root "cpp\dist") -Filter "PicViewerCpp-*-setup.exe" |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Setup) { throw "no installer in cpp\dist; run cpp/packaging/build_installer.sh" }

$AppName    = "Pic Viewer (C++)"
$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\PicViewerCpp"
$Uninstall  = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PicViewerCpp"
$AppKey     = "HKCU:\Software\Classes\Applications\picviewer.exe"
$StartLink  = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$AppName.lnk"
$DeskLink   = Join-Path ([Environment]::GetFolderPath("Desktop")) "$AppName.lnk"

$failures = 0
function Check([string]$what, [bool]$ok) {
    "  {0,-4} {1}" -f $(if ($ok) { "ok" } else { "FAIL" }), $what
    if (-not $ok) { $script:failures++ }
}

function Install {
    $p = Start-Process $Setup.FullName -ArgumentList "/S", "/DESKTOP" -PassThru -Wait
    Check "installer exited 0" ($p.ExitCode -eq 0)
}

"installer: $($Setup.Name), $([int]($Setup.Length / 1MB)) MB"
"`n== install"
Install
Check "picviewer.exe in place"   (Test-Path (Join-Path $InstallDir "picviewer.exe"))
Check "uninstall.ps1 in place"   (Test-Path (Join-Path $InstallDir "uninstall.ps1"))
Check "imageformats plugins"     ((Get-ChildItem (Join-Path $InstallDir "imageformats") -Filter *.dll).Count -ge 5)
Check "Start Menu shortcut"      (Test-Path $StartLink)
Check "desktop shortcut"         (Test-Path $DeskLink)
$entry = Get-ItemProperty $Uninstall -ErrorAction SilentlyContinue
Check "listed in Settings > Apps" ($entry -and $entry.DisplayName -eq $AppName)
Check "size recorded"            ($entry -and $entry.EstimatedSize -gt 50000)
$types = (Get-Item "$AppKey\SupportedTypes" -ErrorAction SilentlyContinue).Property
Check "Open with: 24 extensions" ($types.Count -eq 24)
$command = (Get-ItemProperty "$AppKey\shell\open\command" -ErrorAction SilentlyContinue).'(default)'
Check "Open with: command"       ($command -eq "`"$InstallDir\picviewer.exe`" `"%1`"")

"`n== the installed copy, with no toolchain on PATH"
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root "tools\verify_package.ps1") |
    Select-Object -Last 4
Check "verify_package passed" ($LASTEXITCODE -eq 0)

"`n== uninstall"
# What Settings > Apps runs: the command the install recorded.
$quiet = (Get-ItemProperty $Uninstall).QuietUninstallString
Check "uninstall command recorded" ([bool]$quiet)
$p = Start-Process "cmd.exe" -ArgumentList "/c", $quiet -PassThru -Wait -WindowStyle Hidden
Check "uninstall exited 0" ($p.ExitCode -eq 0)
for ($i = 0; $i -lt 60 -and (Test-Path $InstallDir); $i++) { Start-Sleep -Milliseconds 500 }
Check "install directory removed" (-not (Test-Path $InstallDir))
Check "Start Menu shortcut removed" (-not (Test-Path $StartLink))
Check "desktop shortcut removed"  (-not (Test-Path $DeskLink))
Check "Settings > Apps entry removed" (-not (Test-Path $Uninstall))
Check "Open with entry removed"   (-not (Test-Path $AppKey))
Check "logs and settings kept"    (Test-Path (Join-Path $env:LOCALAPPDATA "PicViewer\logs"))

"`n== install again, to leave it installed"
Install
Check "picviewer.exe in place" (Test-Path (Join-Path $InstallDir "picviewer.exe"))

""
if ($failures) { "$failures check(s) failed"; exit 1 }
"every check passed"
