# Does the installed copy stand on its own?
#
# Run with a PATH stripped back to Windows itself. If anything is still being
# resolved out of the build toolchain, it fails here rather than on somebody
# else's machine.
$exe = Join-Path $env:LOCALAPPDATA "Programs\PicViewerCpp\picviewer.exe"
$log = Join-Path $env:LOCALAPPDATA "PicViewer\logs\picviewer-cpp.log"
$samples = "D:\Project\Claude_Pic_Viewer\samples"
$real = "D:\Project\Claude_Pic_Viewer\image_example"

if (-not (Test-Path $exe)) { throw "not installed: $exe" }

$originalPath = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
Write-Host "PATH reduced to: $env:PATH`n"

$files = @(Get-ChildItem $samples -File | Where-Object { $_.Extension -ne ".md" })
$files += @(Get-ChildItem $real -File | Where-Object { $_.Extension -ne ".md" })

$results = @{}
foreach ($f in $files) {
    Get-Process picviewer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 250
    Clear-Content $log -ErrorAction SilentlyContinue

    Start-Process $exe -ArgumentList "`"$($f.FullName)`""
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt 30000) {
        Start-Sleep -Milliseconds 200
        $t = Get-Content $log -Raw -ErrorAction SilentlyContinue
        if ($t -match "decoded .*$([regex]::Escape($f.Name))" -or $t -match "decode failed|FATAL") { break }
    }
    Get-Process picviewer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 200

    $line = Get-Content $log -ErrorAction SilentlyContinue |
            Where-Object { $_ -match "decoded $([regex]::Escape($f.Name))|decode failed|FATAL" } |
            Select-Object -First 1
    if ($line -match "decoded \S+ via (\S+) in (\d+)ms -> (\d+)x(\d+)") {
        $results[$f.Name] = @{ ok = $true; via = $Matches[1]; ms = [int]$Matches[2]
                               size = "$($Matches[3])x$($Matches[4])" }
    } else {
        $results[$f.Name] = @{ ok = $false; why = if ($line) { $line } else { "no log line" } }
    }
}
$env:PATH = $originalPath

"{0,-38} {1,-6} {2,12} {3,8}" -f "FILE", "VIA", "SIZE", "TIME"
"-" * 68
foreach ($name in ($results.Keys | Sort-Object)) {
    $r = $results[$name]
    if ($r.ok) { "{0,-38} {1,-6} {2,12} {3,6}ms" -f $name, $r.via, $r.size, $r.ms }
    else       { "{0,-38} FAILED  {1}" -f $name, $r.why }
}

$ok = ($results.Values | Where-Object { $_.ok }).Count
"`n  $ok of $($results.Count) decoded with no toolchain on PATH"
foreach ($d in @("qt","heif","raw")) {
    $n = ($results.Values | Where-Object { $_.ok -and $_.via -eq $d }).Count
    "  {0} {1,-5}: {2}" -f $(if ($n -gt 0) { "OK  " } else { "LOST" }), $d, $n
}
