<#
.SYNOPSIS
    Install with the one-file installer, check everything it should have put
    in place, uninstall, check everything is gone, and install again.

.DESCRIPTION
    An installer is the one piece of the build nobody exercises until it is on
    someone else's machine. This runs it silently, end to end, against the
    real per-user locations -- so it replaces whatever build is installed,
    and leaves the new one installed, with a desktop shortcut, when it passes.

    The registration is checked twice over: the keys, and what the shell
    makes of them (cpp\build\assoc_probe.exe). The first installer's keys
    passed every check here while Explorer offered the viewer only behind
    "Choose another app".

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\verify_installer.ps1
#>

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Setup = Get-ChildItem (Join-Path $Root "cpp\dist") -Filter "PicViewerCpp-*-setup.exe" |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Setup) { throw "no installer in cpp\dist; run cpp/packaging/build_installer.sh" }
$Probe = Join-Path $Root "cpp\build\assoc_probe.exe"
if (-not (Test-Path $Probe)) { throw "no $Probe; build cpp first" }

$AppName    = "Pic Viewer"
$OldName    = "Pic Viewer (C++)"
$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\PicViewerCpp"
$Exe        = Join-Path $InstallDir "picviewer.exe"
$Uninstall  = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PicViewerCpp"
$Classes    = "HKCU:\Software\Classes"
$ProgId     = "PicViewerCpp.Image"
$AppKey     = "$Classes\Applications\picviewer.exe"
$CapKey     = "HKCU:\Software\PicViewerCpp"
$Registered = "HKCU:\Software\RegisteredApplications"
$MuiCache   = "$Classes\Local Settings\Software\Microsoft\Windows\Shell\MuiCache"
$StartMenu  = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$Desktop    = [Environment]::GetFolderPath("Desktop")
$Extensions = @(
    ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tif", ".tiff", ".webp",
    ".heic", ".heif", ".avif", ".ico", ".tga", ".ppm",
    ".cr2", ".cr3", ".nef", ".arw", ".dng", ".orf", ".rw2", ".raf",
    ".pef", ".srw"
)

$failures = 0
function Check([string]$what, [bool]$ok) {
    "  {0,-4} {1}" -f $(if ($ok) { "ok" } else { "FAIL" }), $what
    if (-not $ok) { $script:failures++ }
}

function Install {
    $p = Start-Process $Setup.FullName -ArgumentList "/S", "/DESKTOP" -PassThru -Wait
    Check "installer exited 0" ($p.ExitCode -eq 0)
}

function Value([string]$Key, [string]$Name) {
    if (Test-Path $Key) { (Get-Item $Key).GetValue($Name) } else { $null }
}

# What the shell offers for each extension, as "<ext> <recommended> <all> <name>".
function Probe {
    [Console]::OutputEncoding = [Text.Encoding]::UTF8
    & $Probe $Exe @Extensions | ForEach-Object {
        if ($_ -match '^(\S+) recommended=(-?\d+) all=(-?\d+) name=(.*)$') {
            [pscustomobject]@{ Ext = $Matches[1]; Recommended = [int]$Matches[2]
                               All = [int]$Matches[3]; Name = $Matches[4] }
        }
    }
}

"installer: $($Setup.Name), $([int]($Setup.Length / 1MB)) MB"
"`n== install"
Install
Check "picviewer.exe in place"   (Test-Path $Exe)
Check "uninstall.ps1 in place"   (Test-Path (Join-Path $InstallDir "uninstall.ps1"))
Check "imageformats plugins"     ((Get-ChildItem (Join-Path $InstallDir "imageformats") -Filter *.dll).Count -ge 5)
Check "Start Menu shortcut"      (Test-Path -LiteralPath (Join-Path $StartMenu "$AppName.lnk"))
Check "desktop shortcut"         (Test-Path -LiteralPath (Join-Path $Desktop "$AppName.lnk"))
Check "no shortcut by the old name" (-not ((Test-Path -LiteralPath (Join-Path $StartMenu "$OldName.lnk")) -or
                                           (Test-Path -LiteralPath (Join-Path $Desktop "$OldName.lnk"))))
$entry = Get-ItemProperty $Uninstall -ErrorAction SilentlyContinue
Check "listed in Settings > Apps as $AppName" ($entry -and $entry.DisplayName -eq $AppName)
Check "version there is the exe's" ($entry -and $entry.DisplayVersion -eq (Get-Item $Exe).VersionInfo.ProductVersion)
Check "size recorded"            ($entry -and $entry.EstimatedSize -gt 50000)
Check "quiet uninstall recorded" ($entry -and [bool]$entry.QuietUninstallString)

$command = "`"$Exe`" `"%1`""
Check "ProgID opens the exe"     ((Value "$Classes\$ProgId\shell\open\command" "") -eq $command)
$listed = @($Extensions | Where-Object { $null -ne (Value "$Classes\$_\OpenWithProgids" $ProgId) })
Check "ProgID under OpenWithProgids: $($listed.Count) of 24" ($listed.Count -eq 24)
Check "Applications key opens the exe" ((Value "$AppKey\shell\open\command" "") -eq $command)
Check "Applications key named $AppName" ((Value $AppKey "FriendlyAppName") -eq $AppName)
$caps = (Get-Item "$CapKey\Capabilities\FileAssociations" -ErrorAction SilentlyContinue).Property
Check "Capabilities: 24 formats" ($caps.Count -eq 24)
Check "in RegisteredApplications" ((Value $Registered $AppName) -eq "Software\PicViewerCpp\Capabilities")
Check "no cached old name"       ((Value $MuiCache "$Exe.FriendlyAppName") -ne $OldName)

$offers = @(Probe)
Check "shell asked about all 24"  ($offers.Count -eq 24)
$up = @($offers | Where-Object { $_.Recommended -eq 1 })
Check "offered first under Open with: $($up.Count) of 24" ($up.Count -eq 24)
$once = @($offers | Where-Object { $_.All -eq 1 })
Check "listed once, not twice: $($once.Count) of 24" ($once.Count -eq 24)
$named = @($offers | Where-Object { $_.Name -eq $AppName })
Check "shown as ${AppName}: $($named.Count) of 24" ($named.Count -eq 24)
if ($up.Count -ne 24 -or $once.Count -ne 24 -or $named.Count -ne 24) {
    $offers | Where-Object { $_.Recommended -ne 1 -or $_.All -ne 1 -or $_.Name -ne $AppName } |
        ForEach-Object { "         {0} recommended={1} all={2} name={3}" -f $_.Ext, $_.Recommended, $_.All, $_.Name }
}

"`n== the installed copy, with no toolchain on PATH"
& powershell -ExecutionPolicy Bypass -File (Join-Path $Root "tools\verify_package.ps1") |
    Where-Object { $_ -match 'decoded with no toolchain|^\s+(OK|LOST|BAD)\s|FAILED|not in the notices' }
Check "verify_package passed" ($LASTEXITCODE -eq 0)

"`n== uninstall"
# What Settings > Apps runs: the command the install recorded.
$quiet = (Get-ItemProperty $Uninstall).QuietUninstallString
$p = Start-Process "cmd.exe" -ArgumentList "/c", $quiet -PassThru -Wait -WindowStyle Hidden
Check "uninstall exited 0" ($p.ExitCode -eq 0)
for ($i = 0; $i -lt 60 -and (Test-Path $InstallDir); $i++) { Start-Sleep -Milliseconds 500 }
Check "install directory removed" (-not (Test-Path $InstallDir))
Check "Start Menu shortcut removed" (-not (Test-Path -LiteralPath (Join-Path $StartMenu "$AppName.lnk")))
Check "desktop shortcut removed"  (-not (Test-Path -LiteralPath (Join-Path $Desktop "$AppName.lnk")))
Check "Settings > Apps entry removed" (-not (Test-Path $Uninstall))
Check "ProgID removed"            (-not (Test-Path "$Classes\$ProgId"))
$left = @($Extensions | Where-Object { $null -ne (Value "$Classes\$_\OpenWithProgids" $ProgId) })
Check "OpenWithProgids cleared: $($left.Count) left" ($left.Count -eq 0)
Check "Applications key removed"  (-not (Test-Path $AppKey))
Check "Capabilities removed"      (-not (Test-Path $CapKey))
Check "RegisteredApplications cleared" ($null -eq (Value $Registered $AppName))
Check "cached name cleared"       ($null -eq (Value $MuiCache "$Exe.FriendlyAppName"))
$still = @(Probe | Where-Object { $_.All -ne 0 })
Check "shell offers it nowhere: $($still.Count) left" ($still.Count -eq 0)
Check "logs and settings kept"    (Test-Path (Join-Path $env:LOCALAPPDATA "PicViewer\logs"))

"`n== install again, to leave it installed"
Install
Check "picviewer.exe in place" (Test-Path $Exe)
Check "offered first again" (@(Probe | Where-Object { $_.Recommended -eq 1 }).Count -eq 24)

""
if ($failures) { "$failures check(s) failed"; exit 1 }
"every check passed"
