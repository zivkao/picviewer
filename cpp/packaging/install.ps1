<#
.SYNOPSIS
    Install the C++ Pic Viewer for the current user.

.DESCRIPTION
    Same shape as the Python build's installer, and deliberately a separate
    install: different directory, different Start Menu name, different
    uninstall key, so the two can sit side by side while they are being
    compared. Neither overwrites the other.

    Per-user throughout: everything lands under HKCU and the user's own
    AppData, so no administrator prompt, and uninstalling leaves nothing
    behind. Windows 10 and later refuse to let an installer claim a file type
    as the *default* handler, so this registers under "Open with" only.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File cpp\packaging\install.ps1 -DesktopShortcut
#>

[CmdletBinding()]
param(
    [string]$Source,
    [switch]$DesktopShortcut,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

# $PSScriptRoot is not reliably populated while the param block is bound.
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ScriptPath = if ($PSCommandPath) { $PSCommandPath } else { $MyInvocation.MyCommand.Path }
if (-not $Source) { $Source = Join-Path $ScriptDir "..\dist\PicViewer" }

$AppName      = "Pic Viewer (C++)"
$ExeName      = "picviewer.exe"
$Version      = "0.1.0"
$InstallDir   = Join-Path $env:LOCALAPPDATA "Programs\PicViewerCpp"
$StartMenu    = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$Shortcut     = Join-Path $StartMenu "$AppName.lnk"
$DesktopLink  = Join-Path ([Environment]::GetFolderPath("Desktop")) "$AppName.lnk"
$UninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PicViewerCpp"
$AppKey       = "HKCU:\Software\Classes\Applications\$ExeName"

$Extensions = @(
    ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tif", ".tiff", ".webp",
    ".heic", ".heif", ".avif", ".ico", ".tga", ".ppm",
    ".cr2", ".cr3", ".nef", ".arw", ".dng", ".orf", ".rw2", ".raf",
    ".pef", ".srw"
)

function New-Shortcut([string]$Path, [string]$Target) {
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($Path)
    $link.TargetPath = $Target
    $link.WorkingDirectory = Split-Path $Target
    $link.IconLocation = "$Target,0"
    $link.Description = "Image viewer for JPEG, PNG, HEIC, AVIF and camera RAW (C++ build)"
    $link.Save()
}

function Stop-Running {
    $running = Get-Process -Name "picviewer" -ErrorAction SilentlyContinue |
               Where-Object { $_.Path -and $_.Path.StartsWith($InstallDir) }
    if ($running) { $running | Stop-Process -Force; Start-Sleep -Milliseconds 800 }
    return [bool]$running
}

function Remove-PicViewer {
    Write-Host "Removing $AppName..."
    foreach ($path in @($Shortcut, $DesktopLink)) {
        if (Test-Path $path) { Remove-Item $path -Force; Write-Host "  removed $(Split-Path $path -Leaf)" }
    }
    foreach ($key in @($UninstallKey, $AppKey)) {
        if (Test-Path $key) { Remove-Item $key -Recurse -Force; Write-Host "  removed $key" }
    }
    if (Test-Path $InstallDir) {
        # A running copy locks its own DLLs, so say so rather than
        # half-deleting and leaving an install that cannot be repaired.
        if (Get-Process -Name "picviewer" -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -and $_.Path.StartsWith($InstallDir) }) {
            Write-Warning "$AppName is running. Close it and run this again."
            return
        }
        Remove-Item $InstallDir -Recurse -Force
        Write-Host "  removed $InstallDir"
    }
    Write-Host "`nDone. Nothing of $AppName is left." -ForegroundColor Green
}

function Install-PicViewer {
    $resolved = Resolve-Path $Source -ErrorAction SilentlyContinue
    if (-not $resolved) {
        throw "No build at $Source. Gather one first:`n" +
              "  .msys64\usr\bin\bash -lc ""bash cpp/packaging/deploy.sh"""
    }
    $exe = Join-Path $resolved $ExeName
    if (-not (Test-Path $exe)) { throw "$ExeName is not in $resolved" }

    if (Stop-Running) { Write-Warning "$AppName was running; closed it to replace its files." }

    Write-Host "Installing $AppName $Version for $env:USERNAME"
    Write-Host "  into $InstallDir"

    if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force }
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Copy-Item (Join-Path $resolved "*") $InstallDir -Recurse -Force

    $installedExe = Join-Path $InstallDir $ExeName
    $size = (Get-ChildItem $InstallDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
    Write-Host ("  copied {0:N0} MB" -f $size)

    New-Shortcut $Shortcut $installedExe
    Write-Host "  Start Menu shortcut"
    if ($DesktopShortcut) { New-Shortcut $DesktopLink $installedExe; Write-Host "  Desktop shortcut" }

    New-Item -Path "$AppKey\shell\open\command" -Force | Out-Null
    Set-ItemProperty -Path $AppKey -Name "FriendlyAppName" -Value $AppName
    Set-ItemProperty -Path "$AppKey\shell\open\command" -Name "(default)" -Value "`"$installedExe`" `"%1`""
    New-Item -Path "$AppKey\SupportedTypes" -Force | Out-Null
    foreach ($ext in $Extensions) { Set-ItemProperty -Path "$AppKey\SupportedTypes" -Name $ext -Value "" }
    Write-Host "  registered for Open with ($($Extensions.Count) extensions)"

    Copy-Item $ScriptPath (Join-Path $InstallDir "uninstall.ps1") -Force
    New-Item -Path $UninstallKey -Force | Out-Null
    @{
        DisplayName     = $AppName
        DisplayVersion  = $Version
        Publisher       = "ZivKao"
        DisplayIcon     = $installedExe
        InstallLocation = $InstallDir
        UninstallString = "powershell -ExecutionPolicy Bypass -File `"$InstallDir\uninstall.ps1`" -Uninstall"
        NoModify        = 1
        NoRepair        = 1
        EstimatedSize   = [int]($size * 1024)
    }.GetEnumerator() | ForEach-Object { Set-ItemProperty -Path $UninstallKey -Name $_.Key -Value $_.Value }
    Write-Host "  listed in Settings > Apps"

    Write-Host "`nInstalled." -ForegroundColor Green
    Write-Host "  Start Menu: $AppName"
    Write-Host "  The Python build, if installed, is untouched and listed separately."
}

if ($Uninstall) { Remove-PicViewer } else { Install-PicViewer }
