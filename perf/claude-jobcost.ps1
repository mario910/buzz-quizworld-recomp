# Per-job wall cost (setup/run/post) of the hot SPU images in the menu, per
# variant "name=ENV=v;ENV=v". Uses SPURS_JOB_PHASEPROF; reads the last reports.
param([string[]]$Variants, [int]$Seconds = 100)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
foreach ($spec in $Variants) {
    if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue) { throw 'Game already running.' }
    $name, $rest = $spec -split '=', 2
    $set = @()
    if ($rest) { foreach ($kv in $rest -split ';') { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v; $set += $k } }
    # one press (language): the offline frontend has no PSN dialog
    $env:PS3_VERBOSE = '0'; $env:BUZZ_SCRIPT = '12:0x01'; $env:SPURS_JOB_PHASEPROF = '10'
    Remove-Item Env:PS3_FTRACE,Env:PS3_FTRACE_WAIT,Env:BUZZ_TRACE_TRIGGER -ErrorAction SilentlyContinue
    & .\scripts\run-windows.ps1 -Seconds $Seconds -MenuBootstrap -SkipVideo -Mute -Background | Out-Null
    foreach ($k in $set + @('BUZZ_SCRIPT','SPURS_JOB_PHASEPROF')) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
    $log = Get-ChildItem logs -Filter '*.stderr.log' | Sort-Object LastWriteTime | Select-Object -Last 1
    $lines = Get-Content $log.FullName | Select-String 'jobphase\] image=(41|40|39|36) ' | Select-Object -Last 12
    $agg = @{}
    foreach ($l in $lines) {
        if ($l.Line -match 'image=(\d+) n=(\d+) setup=([\d.]+)s run=([\d.]+)s post=([\d.]+)s') {
            $i = $Matches[1]; if (-not $agg[$i]) { $agg[$i] = @(0,0.0,0.0,0.0) }
            $a = $agg[$i]; $a[0] += [double]$Matches[2]; $a[1] += [double]$Matches[3]; $a[2] += [double]$Matches[4]; $a[3] += [double]$Matches[5]; $agg[$i] = $a
        }
    }
    foreach ($i in ($agg.Keys | Sort-Object)) {
        $a = $agg[$i]; $n = $a[0]
        "{0,-8} image={1} n={2} setup={3:N2}us run={4:N2}us post={5:N2}us total={6:N2}us  ({7})" -f $name, $i, $n, (1e6*$a[1]/$n), (1e6*$a[2]/$n), (1e6*$a[3]/$n), (1e6*($a[1]+$a[2]+$a[3])/$n), $log.Name
    }
    Start-Sleep -Seconds 3
}
