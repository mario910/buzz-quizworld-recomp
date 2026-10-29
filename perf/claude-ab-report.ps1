# Summarize A/B traces: host pacing, main wait and JTS tick-aligned share.
param([string]$Pattern = 'claude-dmawake-*.trace')
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
$py = '.\toolenv\Scripts\python.exe'
$tick = Join-Path $root 'claude-jts-tick.py'
foreach ($t in Get-ChildItem -LiteralPath analysis -Filter ($Pattern) | Sort-Object Name) {
    "=== $($t.Name)"
    & $py .\ps3recomp\tools\ftrace_report.py $t.FullName --frame-source present --frames 0 |
        Select-String -Pattern '^# ps3_ftrace|Host presents|Host pacing|tid +1 main|tid +3 Thread|image 41 |RSX|drain  ' | ForEach-Object { $_.Line }
    & $py -I $tick $t.FullName
}
