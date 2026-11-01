# Reading the application's log from a tool, without emptying it.
#
# The log is the user's own, and holds their history for any bug report. The
# tools here used to clear it before each run so that what they read back was
# theirs; now each notes how long the log is before it starts and reads only
# what was appended after that.
#
#   . (Join-Path $PSScriptRoot "logtail.ps1")
#   $from = Get-LogSize $log
#   ... run the viewer ...
#   $text = Read-LogSince $log $from

function Get-LogSize([string]$Log) {
    if (Test-Path $Log) { (Get-Item $Log).Length } else { 0 }
}

function Read-LogSince([string]$Log, [long]$Offset) {
    if (-not (Test-Path $Log)) { return "" }
    $stream = [IO.File]::Open($Log, 'Open', 'Read', 'ReadWrite')
    try {
        # Shorter than before means it rotated meanwhile: read it all.
        if ($stream.Length -lt $Offset) { $Offset = 0 }
        $stream.Seek($Offset, 'Begin') | Out-Null
        (New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8)).ReadToEnd()
    } finally {
        $stream.Close()
    }
}
