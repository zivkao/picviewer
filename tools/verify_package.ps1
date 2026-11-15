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
. (Join-Path $PSScriptRoot "logtail.ps1")

$originalPath = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
Write-Host "PATH reduced to: $env:PATH`n"

$files = @(Get-ChildItem $samples -File | Where-Object { $_.Extension -ne ".md" })
$files += @(Get-ChildItem $real -File | Where-Object { $_.Extension -ne ".md" })

$results = @{}
foreach ($f in $files) {
    Get-Process picviewer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 250
    $from = Get-LogSize $log

    Start-Process $exe -ArgumentList "`"$($f.FullName)`""
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt 30000) {
        Start-Sleep -Milliseconds 200
        $t = Read-LogSince $log $from
        if ($t -match "decoded .*$([regex]::Escape($f.Name))" -or $t -match "decode failed|FATAL") { break }
    }
    Get-Process picviewer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 200

    $line = (Read-LogSince $log $from) -split "`n" |
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
$bad = $results.Count - $ok
"`n  $ok of $($results.Count) decoded with no toolchain on PATH"
foreach ($d in @("qt","heif","raw")) {
    $n = ($results.Values | Where-Object { $_.ok -and $_.via -eq $d }).Count
    "  {0} {1,-5}: {2}" -f $(if ($n -gt 0) { "OK  " } else { "LOST" }), $d, $n
    if ($n -eq 0) { $bad++ }
}

# Every library shipped is accounted for in the notices, under its own
# licence, and the project's own licence is beside it. notices.sh writes both
# from the package's files; this reads them back from what was installed.
$dir = Split-Path $exe
$notices = Join-Path $dir "THIRD-PARTY-NOTICES.txt"
$listed = @{}
if (Test-Path $notices) {
    foreach ($line in (Get-Content $notices)) {
        if ($line -match '^\s+files:\s+(.+)$') {
            foreach ($name in ($Matches[1] -split '\s+')) { $listed[$name] = $true }
        }
    }
}
$dlls = @(Get-ChildItem $dir -Recurse -Filter *.dll |
          ForEach-Object { $_.FullName.Substring($dir.Length + 1).Replace('\', '/') })
$unlisted = @($dlls | Where-Object { -not $listed.ContainsKey($_) })
$licence = Test-Path (Join-Path $dir "LICENSE.txt")
"  {0} notices: {1} of {2} libraries listed, LICENSE.txt {3}" -f `
    $(if ($unlisted.Count -eq 0 -and $dlls.Count -gt 0 -and $licence) { "OK  " } else { "BAD " }),
    ($dlls.Count - $unlisted.Count), $dlls.Count, $(if ($licence) { "present" } else { "MISSING" })
foreach ($u in $unlisted) { "       not in the notices: $u" }
if ($unlisted.Count -gt 0 -or $dlls.Count -eq 0 -or -not $licence) { $bad++ }

# verify_installer.ps1 judges this script by its exit code. Until 2026-11-16
# it always exited 0: a file that failed to decode was printed, and passed.
if ($bad -gt 0) { exit 1 }
exit 0
