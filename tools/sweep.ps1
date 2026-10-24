# Open every sample with the C++ build, one at a time, and read its own log
# back. Generous waits: the previous sweep killed the process mid-decode and
# made working formats look broken.
$env:PATH = "D:\Project\Claude_Pic_Viewer\.msys64\mingw64\bin;$env:PATH"
$Exe = "D:\Project\Claude_Pic_Viewer\cpp\build\picviewer.exe"
$Samples = "D:\Project\Claude_Pic_Viewer\samples"
$Log = Join-Path $env:LOCALAPPDATA "PicViewer\logs\picviewer-cpp.log"

Get-Process picviewer -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
if (Test-Path $Log) { Clear-Content $Log }

$files = Get-ChildItem $Samples -File | Where-Object { $_.Extension -ne ".md" }
Write-Host "opening $($files.Count) samples`n"

foreach ($f in $files) {
    Start-Process $Exe -ArgumentList "`"$($f.FullName)`""
    Start-Sleep -Milliseconds 1800
    Get-Process picviewer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 250
}
Start-Sleep -Seconds 1

$lines = Get-Content $Log
$decoded = @{}
foreach ($line in ($lines | Select-String 'decoded ')) {
    # Foreground decodes only: a prefetch line says nothing about what the
    # window was actually asked to show.
    if ($line.Line -notmatch '\[prefetch\]' -and
        $line.Line -match 'decoded (\S+) via (\S+) in (\d+)ms -> (\d+)x(\d+)') {
        $decoded[$Matches[1]] = @{ decoder = $Matches[2]; ms = [int]$Matches[3]
                                   size = "$($Matches[4])x$($Matches[5])" }
    }
}

Write-Host "=== foreground decode of each sample ==="
"  {0,-24} {1,-6} {2,10} {3,>7}" -f "FILE", "VIA", "SIZE", "TIME"
foreach ($name in ($decoded.Keys | Sort-Object)) {
    $d = $decoded[$name]
    "  {0,-24} {1,-6} {2,10} {3,5}ms" -f $name, $d.decoder, $d.size, $d.ms
}

Write-Host "`n=== every decoder exercised? ==="
foreach ($want in @("qt", "heif", "raw")) {
    $hits = ($decoded.Values | Where-Object { $_.decoder -eq $want }).Count
    "  {0} {1,-5}: {2} file(s)" -f $(if ($hits -gt 0) { "OK  " } else { "LOST" }), $want, $hits
}

Write-Host "`n=== checks ==="
$r = $decoded["rotated_exif.jpg"]
if ($r) { "  EXIF rotation : $($r.size)  $(if ($r.size -eq '400x600') {'applied'} else {'IGNORED'})" }
$i = $decoded["sample.ico"]
if ($i) { "  ICO largest   : $($i.size)  $(if ($i.size -eq '256x256') {'largest frame'} else {'SMALLEST frame'})" }
$a = $decoded["actually_png.jpg"]
if ($a) { "  lying .jpg    : decoded via $($a.decoder) at $($a.size)" }
"  preview stage : " + (($lines | Select-String 'preview for').Count) + " emitted"
$errs = $lines | Select-String "ERROR"
"  errors        : " + $(if ($errs) { $errs.Count } else { "none" })
if ($errs) { $errs | Select-Object -First 3 | ForEach-Object { "    " + ($_.Line -replace '^\S+\s+\S+\s+','') } }
"  decoded $($decoded.Count) of $($files.Count) samples"
