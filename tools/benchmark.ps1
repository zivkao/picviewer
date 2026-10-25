<#
.SYNOPSIS
    Measure both builds and compare against the committed baseline.

.DESCRIPTION
    The numbers that justify decisions about this project -- is the C++ port
    worth keeping, did a change make decoding slower -- were living in prose
    tables and in one conversation. Prose cannot be diffed. This writes them
    in a form a later run can compare itself against.

    Measures, per build: time from launch to a window on screen, decode time
    per sample, and installed size. Decode times come from the application's
    own log rather than from wall-clock timing around the process, so they
    exclude startup and are the figure that actually moved when a decoder
    changed.

.PARAMETER Update
    Overwrite tools/baseline.json with this run. Do this when a change is
    meant to move the numbers, and commit the result with it.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\benchmark.ps1
    powershell -ExecutionPolicy Bypass -File tools\benchmark.ps1 -Update
#>

[CmdletBinding()]
param(
    [switch]$Update,
    [int]$Runs = 5
)

$ErrorActionPreference = "Stop"
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$Root = Resolve-Path (Join-Path $ScriptDir "..")
$BaselinePath = Join-Path $ScriptDir "baseline.json"

$Builds = @(
    @{ name = "python"
       exe  = Join-Path $env:LOCALAPPDATA "Programs\PicViewer\PicViewer.exe"
       log  = Join-Path $env:LOCALAPPDATA "PicViewer\logs\picviewer.log"
       proc = "PicViewer" }
    @{ name = "cpp"
       exe  = Join-Path $env:LOCALAPPDATA "Programs\PicViewerCpp\picviewer.exe"
       log  = Join-Path $env:LOCALAPPDATA "PicViewer\logs\picviewer-cpp.log"
       proc = "picviewer" }
)

# A small fixed set: one per decoder, plus whatever real-world files are
# present. The samples are committed so the figures stay comparable; the real
# files are not, so they are reported only when they are there.
$Cases = @(
    @{ id = "jpeg";   path = Join-Path $Root "samples\sample.jpg" }
    @{ id = "png";    path = Join-Path $Root "samples\sample.png" }
    @{ id = "tiff";   path = Join-Path $Root "samples\sample.tif" }
    @{ id = "avif";   path = Join-Path $Root "samples\sample.avif" }
    @{ id = "heif";   path = Join-Path $Root "samples\sample.heic" }
    @{ id = "raw_synthetic"; path = Join-Path $Root "samples\synthetic.dng" }
)
foreach ($f in (Get-ChildItem (Join-Path $Root "image_example") -File -ErrorAction SilentlyContinue)) {
    if ($f.Extension -eq ".md") { continue }
    # One id per format, not one for "real RAW": with four makers' files
    # present a shared id let each overwrite the last, and the baseline kept
    # whichever happened to be listed last.
    $ext = $f.Extension.TrimStart('.').ToLower()
    $id = switch -Regex ($ext) {
        '^(cr2|cr3|nef|arw|dng|orf|rw2|raf|pef|srw)$' { "raw_$ext" }
        '^(heic|heif|avif)$'                          { "real_$ext" }
        default                                       { "large_$ext" }
    }
    $Cases += @{ id = $id; path = $f.FullName; untracked = $true }
}

function Stop-Build($proc) {
    Get-Process $proc -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 250
}

function Measure-Startup($build) {
    $times = @()
    $sample = Join-Path $Root "samples\sample.jpg"
    foreach ($i in 1..$Runs) {
        Stop-Build $build.proc
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process $build.exe -ArgumentList "`"$sample`"" -PassThru
        while ($sw.ElapsedMilliseconds -lt 20000) {
            $proc = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
            if ($proc -and $proc.MainWindowHandle -ne 0) { break }
            Start-Sleep -Milliseconds 10
        }
        $sw.Stop()
        $times += [int]$sw.ElapsedMilliseconds
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 200
    }
    # Median, not mean: the first launch of a session is a cold-cache outlier
    # several times the rest, and it would drag an average anywhere it liked.
    ($times | Sort-Object)[[int]($times.Count / 2)]
}

function Measure-Decode($build, $case) {
    Stop-Build $build.proc
    if (Test-Path $build.log) { Clear-Content $build.log }

    Start-Process $build.exe -ArgumentList "`"$($case.path)`"" | Out-Null
    $name = Split-Path $case.path -Leaf
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $ms = $null; $via = $null; $size = $null
    while ($sw.ElapsedMilliseconds -lt 40000) {
        Start-Sleep -Milliseconds 150
        $text = Get-Content $build.log -Raw -ErrorAction SilentlyContinue
        if ($text -match "decoded \S*$([regex]::Escape($name))\S* via (\S+) in (\d+)ms -> (\d+x\d+)") {
            $via = $Matches[1]; $ms = [int]$Matches[2]; $size = $Matches[3]; break
        }
        if ($text -match "decode failed") { break }
    }
    Stop-Build $build.proc
    if ($null -eq $ms) { return $null }
    @{ ms = $ms; via = $via; size = $size }
}

function Measure-Size($build) {
    $dir = Split-Path $build.exe
    if (-not (Test-Path $dir)) { return $null }
    [int]((Get-ChildItem $dir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
}

# ---------------------------------------------------------------------------

$result = [ordered]@{
    recorded = (Get-Date).ToString("yyyy-MM-dd HH:mm")
    machine  = @{ os = (Get-CimInstance Win32_OperatingSystem).Caption
                  cores = [Environment]::ProcessorCount }
    builds   = [ordered]@{}
}

foreach ($build in $Builds) {
    if (-not (Test-Path $build.exe)) {
        Write-Host "$($build.name): not installed, skipping"
        continue
    }
    Write-Host "measuring $($build.name)..."
    $entry = [ordered]@{
        startup_ms = Measure-Startup $build
        size_mb    = Measure-Size $build
        decode     = [ordered]@{}
    }
    foreach ($case in $Cases) {
        if (-not (Test-Path $case.path)) { continue }
        $m = Measure-Decode $build $case
        if ($m) { $entry.decode[$case.id] = $m }
    }
    $result.builds[$build.name] = $entry
}

# ---------------------------------------------------------------------------

function Show-Delta($label, $now, $was, $unit, $lowerIsBetter = $true) {
    if ($null -eq $now) { return }
    if ($null -eq $was) {
        "  {0,-22} {1,8} {2}" -f $label, $now, $unit
        return
    }
    $delta = $now - $was
    $pct = if ($was -ne 0) { [math]::Round(100 * $delta / $was) } else { 0 }
    $mark = if ([math]::Abs($pct) -lt 10) { "" }
            elseif (($delta -lt 0) -eq $lowerIsBetter) { "  better" }
            else { "  WORSE" }
    "  {0,-22} {1,8} {2}   was {3}{4} ({5:+#;-#;0}%){6}" -f $label, $now, $unit, $was, $unit, $pct, $mark
}

$baseline = if (Test-Path $BaselinePath) { Get-Content $BaselinePath -Raw | ConvertFrom-Json } else { $null }

foreach ($name in $result.builds.Keys) {
    Write-Host "`n=== $name ==="
    $now = $result.builds[$name]
    $was = if ($baseline) { $baseline.builds.$name } else { $null }

    Show-Delta "launch to window" $now.startup_ms $(if ($was) { $was.startup_ms }) "ms"
    Show-Delta "installed size"   $now.size_mb    $(if ($was) { $was.size_mb })    "MB"
    foreach ($id in $now.decode.Keys) {
        Show-Delta "decode $id" $now.decode[$id].ms $(if ($was -and $was.decode.$id) { $was.decode.$id.ms }) "ms"
    }
}

if ($Update) {
    $result | ConvertTo-Json -Depth 6 | Set-Content $BaselinePath -Encoding UTF8
    Write-Host "`nbaseline updated: $BaselinePath"
    Write-Host "commit it alongside the change that moved the numbers."
} elseif (-not $baseline) {
    Write-Host "`nno baseline yet -- run with -Update to record this one."
} else {
    Write-Host "`nbaseline from $($baseline.recorded); -Update to replace it."
}
