<#
.SYNOPSIS
    Install, register or remove Pic Viewer for the current user.

.DESCRIPTION
    The one place that knows what an install puts where. It is copied into the
    install directory as uninstall.ps1, and the one-file installer copies the
    files and then runs that copy with -Register, so the two ways of
    installing cannot drift apart.

    Per-user throughout: everything lands under HKCU and the user's own
    AppData, so no administrator prompt.

    Windows 10 and later refuse to let an installer make itself the default
    for a file type. What an installer can do is register properly, so the
    choice is one click for the user rather than a hunt:

      - a ProgID, PicViewerCpp.Image, listed under each extension's
        OpenWithProgids: that is what puts it in the list "Open with" shows
        first. The Applications\picviewer.exe key alone, which is all the
        first installer wrote, lists it only behind "Choose another app",
        beside Notepad and Word.
      - Capabilities, named in RegisteredApplications: that is what gives it
        a page of its own in Settings > Apps > Default apps.

    Uninstalling removes all of it. It keeps %LOCALAPPDATA%\PicViewer --
    settings, logs and the thumbnail cache -- so a reinstall picks up where
    the last one left off; delete that folder to remove those too.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File cpp\packaging\install.ps1 -DesktopShortcut
#>

[CmdletBinding()]
param(
    [string]$Source,
    [switch]$DesktopShortcut,
    [switch]$Register,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

# $PSScriptRoot is not reliably populated while the param block is bound.
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ScriptPath = if ($PSCommandPath) { $PSCommandPath } else { $MyInvocation.MyCommand.Path }
if (-not $Source) { $Source = Join-Path $ScriptDir "..\dist\PicViewer" }

$AppName      = "Pic Viewer"
# Called "Pic Viewer (C++)" while the Python build was installed beside it.
# Shortcuts by the old name are removed, and a desktop one replaced.
$OldNames     = @("Pic Viewer (C++)")
$Description  = "Image viewer for JPEG, PNG, HEIC, AVIF and camera RAW"
$ExeName      = "picviewer.exe"
$InstallDir   = Join-Path $env:LOCALAPPDATA "Programs\PicViewerCpp"
$InstalledExe = Join-Path $InstallDir $ExeName
$DataDir      = Join-Path $env:LOCALAPPDATA "PicViewer"
$StartMenu    = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$Desktop      = [Environment]::GetFolderPath("Desktop")
$UninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PicViewerCpp"
$Classes      = "HKCU:\Software\Classes"
$ProgId       = "PicViewerCpp.Image"
$AppKey       = "$Classes\Applications\$ExeName"
$Capabilities = "Software\PicViewerCpp\Capabilities"
$Registered   = "HKCU:\Software\RegisteredApplications"
$MuiCache     = "$Classes\Local Settings\Software\Microsoft\Windows\Shell\MuiCache"

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
    $link.Description = $Description
    $link.Save()
}

function Set-Values([string]$Key, [hashtable]$Values) {
    if (-not (Test-Path $Key)) { New-Item -Path $Key -Force | Out-Null }
    foreach ($name in $Values.Keys) { Set-ItemProperty -Path $Key -Name $name -Value $Values[$name] }
}

function Remove-Value([string]$Key, [string]$Name) {
    if ((Test-Path $Key) -and ($null -ne (Get-Item $Key).GetValue($Name))) {
        Remove-ItemProperty -Path $Key -Name $Name
    }
}

# A key left with nothing in it says nothing; one that holds anything is left.
function Remove-IfEmpty([string]$Key) {
    if (Test-Path $Key) {
        $item = Get-Item $Key
        if ($item.ValueCount -eq 0 -and $item.SubKeyCount -eq 0) { Remove-Item $Key }
    }
}

# Explorer caches each program's display name by path. Left alone, it went
# on calling the renamed build "Pic Viewer (C++)".
function Clear-NameCache {
    if (Test-Path $MuiCache) {
        foreach ($suffix in ".FriendlyAppName", ".ApplicationCompany") {
            Remove-Value $MuiCache "$InstalledExe$suffix"
        }
    }
}

function Send-AssocChanged {
    # SHCNE_ASSOCCHANGED: Explorer re-reads associations now rather than at
    # the next sign-in.
    if (-not ("PicViewerSetup.Shell" -as [type])) {
        Add-Type -Namespace PicViewerSetup -Name Shell -MemberDefinition `
            '[DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, uint f, IntPtr a, IntPtr b);'
    }
    [PicViewerSetup.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

function Remove-Shortcuts([string[]]$Names) {
    foreach ($name in $Names) {
        foreach ($dir in $StartMenu, $Desktop) {
            $path = Join-Path $dir "$name.lnk"
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force; Write-Host "  removed $path" }
        }
    }
}

function Register-PicViewer {
    if (-not (Test-Path $InstalledExe)) { throw "$InstalledExe is not there to register" }
    $command = "`"$InstalledExe`" `"%1`""
    $icon = "$InstalledExe,0"

    # A desktop shortcut under the old name was asked for once; keep it.
    $hadDesktop = [bool]($OldNames | Where-Object { Test-Path -LiteralPath (Join-Path $Desktop "$_.lnk") })
    Remove-Shortcuts $OldNames
    New-Shortcut (Join-Path $StartMenu "$AppName.lnk") $InstalledExe
    Write-Host "  Start Menu shortcut"
    if ($DesktopShortcut -or $hadDesktop) {
        New-Shortcut (Join-Path $Desktop "$AppName.lnk") $InstalledExe
        Write-Host "  desktop shortcut"
    }

    # The ProgID: what each extension's "Open with" points at. No type name
    # is set, so Explorer's Type column goes on saying "JPG File", "PNG File"
    # rather than one name for every format.
    Set-Values "$Classes\$ProgId" @{}
    Set-Values "$Classes\$ProgId\DefaultIcon" @{ "(default)" = $icon }
    Set-Values "$Classes\$ProgId\shell\open\command" @{ "(default)" = $command }
    Set-Values "$Classes\$ProgId\Application" @{
        ApplicationName = $AppName; ApplicationIcon = $icon; ApplicationDescription = $Description
    }
    foreach ($ext in $Extensions) { Set-Values "$Classes\$ext\OpenWithProgids" @{ $ProgId = "" } }

    # The application key: the name and command for picviewer.exe itself,
    # which Windows uses when it was chosen through "Choose another app".
    Set-Values $AppKey @{ FriendlyAppName = $AppName }
    Set-Values "$AppKey\shell\open\command" @{ "(default)" = $command }
    Set-Values "$AppKey\SupportedTypes" @{}
    foreach ($ext in $Extensions) { Set-ItemProperty -Path "$AppKey\SupportedTypes" -Name $ext -Value "" }

    # Settings > Apps > Default apps.
    Set-Values "HKCU:\$Capabilities" @{
        ApplicationName = $AppName; ApplicationIcon = $icon; ApplicationDescription = $Description
    }
    $assoc = @{}
    foreach ($ext in $Extensions) { $assoc[$ext] = $ProgId }
    Set-Values "HKCU:\$Capabilities\FileAssociations" $assoc
    Set-Values $Registered @{ $AppName = $Capabilities }
    Write-Host "  registered for $($Extensions.Count) formats: Open with, Settings > Default apps"

    # Settings > Apps. The version is the one built into the exe.
    $version = (Get-Item $InstalledExe).VersionInfo.ProductVersion
    $size = (Get-ChildItem $InstallDir -Recurse -File | Measure-Object Length -Sum).Sum / 1KB
    $remove = "powershell -ExecutionPolicy Bypass -File `"$InstallDir\uninstall.ps1`" -Uninstall"
    Set-Values $UninstallKey @{
        DisplayName          = $AppName
        DisplayVersion       = $version
        Publisher            = "ZivKao"
        DisplayIcon          = $InstalledExe
        InstallLocation      = $InstallDir
        UninstallString      = $remove
        QuietUninstallString = $remove
    }
    foreach ($pair in @{ NoModify = 1; NoRepair = 1; EstimatedSize = [int]$size }.GetEnumerator()) {
        Set-ItemProperty -Path $UninstallKey -Name $pair.Key -Value $pair.Value -Type DWord
    }
    Write-Host "  listed in Settings > Apps as $AppName $version"

    Clear-NameCache
    Send-AssocChanged
}

function Remove-PicViewer {
    Write-Host "Removing $AppName..."
    # A running copy locks its own DLLs. Checked first, so a refusal leaves
    # the install whole rather than half-registered.
    if (Get-Process -Name "picviewer" -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($InstallDir) }) {
        Write-Warning "$AppName is running. Close it and run this again."
        exit 1
    }

    Remove-Shortcuts (@($AppName) + $OldNames)

    foreach ($ext in $Extensions) {
        Remove-Value "$Classes\$ext\OpenWithProgids" $ProgId
        Remove-IfEmpty "$Classes\$ext\OpenWithProgids"
        Remove-IfEmpty "$Classes\$ext"
    }
    Remove-Value $Registered $AppName
    foreach ($key in "$Classes\$ProgId", $AppKey, "HKCU:\Software\PicViewerCpp", $UninstallKey) {
        if (Test-Path $key) { Remove-Item $key -Recurse -Force; Write-Host "  removed $key" }
    }
    Clear-NameCache
    Send-AssocChanged

    if (Test-Path $InstallDir) {
        Remove-Item $InstallDir -Recurse -Force
        Write-Host "  removed $InstallDir"
    }

    Write-Host "`nRemoved: the program, its shortcuts, and its registration with Windows." -ForegroundColor Green
    if (Test-Path $DataDir) {
        Write-Host "Kept: your settings, logs and thumbnail cache, in $DataDir"
        Write-Host "Delete that folder to remove those too."
    }
}

function Install-PicViewer {
    $resolved = Resolve-Path $Source -ErrorAction SilentlyContinue
    if (-not $resolved) {
        throw "No build at $Source. Gather one first:`n" +
              "  .msys64\usr\bin\bash -lc ""bash cpp/packaging/deploy.sh"""
    }
    if (-not (Test-Path (Join-Path $resolved $ExeName))) { throw "$ExeName is not in $resolved" }

    $running = Get-Process -Name "picviewer" -ErrorAction SilentlyContinue |
               Where-Object { $_.Path -and $_.Path.StartsWith($InstallDir) }
    if ($running) {
        $running | Stop-Process -Force
        Start-Sleep -Milliseconds 800
        Write-Warning "$AppName was running; closed it to replace its files."
    }

    Write-Host "Installing $AppName for $env:USERNAME"
    Write-Host "  into $InstallDir"
    if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force }
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Copy-Item (Join-Path $resolved "*") $InstallDir -Recurse -Force
    Copy-Item $ScriptPath (Join-Path $InstallDir "uninstall.ps1") -Force
    $size = (Get-ChildItem $InstallDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
    Write-Host ("  copied {0:N0} MB" -f $size)

    Register-PicViewer

    Write-Host "`nInstalled." -ForegroundColor Green
    Write-Host "  To make it the default viewer: Settings > Apps > Default apps > $AppName."
    Write-Host "  (Windows lets only you set that, not an installer.)"
}

if ($Uninstall) { Remove-PicViewer }
elseif ($Register) { Register-PicViewer }
else { Install-PicViewer }
