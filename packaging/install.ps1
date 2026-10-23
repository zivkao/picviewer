<#
.SYNOPSIS
    Install Pic Viewer for the current user.

.DESCRIPTION
    Copies the build into %LOCALAPPDATA%\Programs\PicViewer, adds a Start Menu
    shortcut, registers the app so it appears under "Open with" for the formats
    it reads, and adds an entry to Settings > Apps so it can be removed the
    normal way.

    Per-user on purpose: everything lands under HKCU and the user's own
    AppData, so no administrator prompt, and uninstalling leaves nothing
    behind. It also cannot be set as the *default* handler from here -- Windows
    10 and later deliberately refuse that to stop programs hijacking
    associations. Use Settings > Default apps for that, once.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging\install.ps1
#>

[CmdletBinding()]
param(
    [string]$Source,
    [switch]$DesktopShortcut,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

# Resolved here rather than as a param default: $PSScriptRoot is not reliably
# populated while the param block is being bound.
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ScriptPath = if ($PSCommandPath) { $PSCommandPath } else { $MyInvocation.MyCommand.Path }
if (-not $Source) { $Source = Join-Path $ScriptDir "..\dist\PicViewer" }

$AppName     = "Pic Viewer"
$ExeName     = "PicViewer.exe"
$Version     = "0.1.0"
$InstallDir  = Join-Path $env:LOCALAPPDATA "Programs\PicViewer"
$StartMenu   = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$Shortcut    = Join-Path $StartMenu "$AppName.lnk"
$DesktopLink = Join-Path ([Environment]::GetFolderPath("Desktop")) "$AppName.lnk"
$UninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PicViewer"
$AppKey      = "HKCU:\Software\Classes\Applications\$ExeName"

# What to offer under "Open with". Kept to the formats actually worth opening
# this way -- registering all 65 extensions would clutter the menu for files
# nobody double-clicks.
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
    $link.Description = "Image viewer for JPEG, PNG, HEIC, AVIF and camera RAW"
    $link.Save()
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
        # A running copy locks its own DLLs, so say so rather than half-deleting.
        $running = Get-Process -Name "PicViewer" -ErrorAction SilentlyContinue
        if ($running) {
            Write-Warning "$AppName is running. Close it and run this again."
            return
        }
        Remove-Item $InstallDir -Recurse -Force
        Write-Host "  removed $InstallDir"
    }

    Write-Host "`nDone. Nothing of $AppName is left." -ForegroundColor Green
}

function Install-PicViewer {
    $Source = (Resolve-Path $Source -ErrorAction SilentlyContinue)
    if (-not $Source) {
        throw "Build not found. Run this first:`n" +
              "  .venv\Scripts\python.exe -m PyInstaller packaging/picviewer.spec --noconfirm"
    }
    $exe = Join-Path $Source $ExeName
    if (-not (Test-Path $exe)) { throw "$ExeName is not in $Source" }

    $running = Get-Process -Name "PicViewer" -ErrorAction SilentlyContinue
    if ($running) {
        Write-Warning "$AppName is running; closing it so its files can be replaced."
        $running | Stop-Process -Force
        Start-Sleep -Seconds 1
    }

    Write-Host "Installing $AppName $Version for $env:USERNAME"
    Write-Host "  into $InstallDir"

    if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force }
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Copy-Item (Join-Path $Source "*") $InstallDir -Recurse -Force

    $installedExe = Join-Path $InstallDir $ExeName
    $size = (Get-ChildItem $InstallDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
    Write-Host ("  copied {0:N0} MB" -f $size)

    New-Shortcut $Shortcut $installedExe
    Write-Host "  Start Menu shortcut"
    if ($DesktopShortcut) {
        New-Shortcut $DesktopLink $installedExe
        Write-Host "  Desktop shortcut"
    }

    # "Open with" registration. SupportedTypes is what puts the app in that
    # menu for these extensions without claiming any of them as default.
    New-Item -Path "$AppKey\shell\open\command" -Force | Out-Null
    Set-ItemProperty -Path $AppKey -Name "FriendlyAppName" -Value $AppName
    Set-ItemProperty -Path "$AppKey\shell\open\command" -Name "(default)" -Value "`"$installedExe`" `"%1`""
    New-Item -Path "$AppKey\SupportedTypes" -Force | Out-Null
    foreach ($ext in $Extensions) {
        Set-ItemProperty -Path "$AppKey\SupportedTypes" -Name $ext -Value ""
    }
    Write-Host "  registered for Open with ($($Extensions.Count) extensions)"

    # Settings > Apps entry, so it uninstalls like anything else.
    $uninstallCommand = "powershell -ExecutionPolicy Bypass -File `"$InstallDir\uninstall.ps1`""
    Copy-Item $ScriptPath (Join-Path $InstallDir "uninstall.ps1") -Force
    New-Item -Path $UninstallKey -Force | Out-Null
    @{
        DisplayName     = $AppName
        DisplayVersion  = $Version
        Publisher       = "ZivKao"
        DisplayIcon     = $installedExe
        InstallLocation = $InstallDir
        UninstallString = "$uninstallCommand -Uninstall"
        NoModify        = 1
        NoRepair        = 1
        EstimatedSize   = [int]($size * 1024)
    }.GetEnumerator() | ForEach-Object {
        Set-ItemProperty -Path $UninstallKey -Name $_.Key -Value $_.Value
    }
    Write-Host "  listed in Settings > Apps"

    Write-Host "`nInstalled." -ForegroundColor Green
    Write-Host "  Start Menu: $AppName"
    Write-Host "  To make it the default for a format: right-click an image >"
    Write-Host "  Open with > Choose another app > Pic Viewer > Always."
    Write-Host "  (Windows does not let an installer set that for you.)"
}

if ($Uninstall) { Remove-PicViewer } else { Install-PicViewer }
