# Boot -> "Wyzwanie nad krawedzia" with profile Adam, by buzzer keys (Space=red,
# 2=orange), then let the round run. Optional env "K=V;K2=V2" for the run.
#   claude-solo-auto.ps1 [-Env 'PERF=1'] [-RoundSeconds 60] [-Shots]
#     -Challenge N   down presses in the solo-challenge list (0 = Wyzwanie nad krawedzia)
#     -AnswerEvery S press an answer button (cycling blue/orange/green/yellow) every S s
#     -Slow F        stretch every fixed delay by F (compare modes run slower)
#     -CapDir D      rsxcap 3 frames (with textures) 30 s into the round, into D
#     -PsnPrompt     answer the PSN error dialog (only with BUZZ_OFFLINE=0; the
#                    default offline frontend boots without it)
param([string]$Env = '', [int]$RoundSeconds = 60, [switch]$Shots, [string]$Trace = '',
      [int]$Challenge = 0, [int]$AnswerEvery = 0, [double]$Slow = 1.0, [switch]$PsnPrompt,
      [string]$CapDir = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue) { throw 'Game already running.' }
$ctl = Join-Path $PSScriptRoot 'claude-gamectl.ps1'
$shotDir = Join-Path $PSScriptRoot 'claude-solo-shots'
New-Item -ItemType Directory -Force $shotDir | Out-Null
$set = @()
if ($Env) { foreach ($kv in $Env -split ';') { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v; $set += $k } }
$env:PS3_VERBOSE = '0'
Remove-Item Env:BUZZ_SCRIPT,Env:PS3_FTRACE,Env:PS3_FTRACE_WAIT,Env:BUZZ_TRACE_TRIGGER -ErrorAction SilentlyContinue
$marker = ''
if ($Trace) {
    $marker = "$Trace.trigger"
    if ((Test-Path $Trace) -or (Test-Path $marker)) { throw "exists: $Trace" }
    $env:PS3_FTRACE_WAIT = '1'; $env:PS3_FTRACE = "0:20:$Trace"; $env:BUZZ_TRACE_TRIGGER = $marker
}
$capTrigger = ''
if ($CapDir) {
    New-Item -ItemType Directory -Force $CapDir | Out-Null
    $capTrigger = Join-Path $CapDir 'trigger.txt'; Set-Content $capTrigger '0'
    $env:RSX_CAP = '1:3:1'; $env:RSX_CAP_DIR = $CapDir; $env:RSX_CAP_TRIGGER_FILE = $capTrigger; $env:RSX_CAP_TEXTURES = '1'
    $set += @('RSX_CAP','RSX_CAP_DIR','RSX_CAP_TRIGGER_FILE','RSX_CAP_TEXTURES')
}
$out = .\scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap -SkipVideo -Mute -Background | Out-String
foreach ($k in $set + @('PS3_FTRACE','PS3_FTRACE_WAIT','BUZZ_TRACE_TRIGGER')) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
$procId = [int]([regex]::Match($out, 'PID=(\d+)').Groups[1].Value)
$log = [regex]::Match($out, 'stderr=(\S.*?\.stderr\.log)').Groups[1].Value
function Title { (Get-Process -Id $procId).MainWindowTitle }
function Key([string]$vk) { & $ctl key $vk -HoldMs 300 | Out-Null }
function Wait([double]$s) { Start-Sleep -Milliseconds ([int]($s * 1000 * $Slow)) }
function WaitDraws([int]$min, [int]$timeout) {
    $deadline = (Get-Date).AddSeconds($timeout)
    do { Start-Sleep -Milliseconds 500 } until (((Title) -match 'draws: (\d+)' -and [int]$Matches[1] -gt $min) -or (Get-Date) -gt $deadline)
}
WaitDraws 0 (120 * $Slow); Wait 8
Key 0x20; Wait 6                  # language
Key 0x20; Wait 25                 # buzzer info
if ($PsnPrompt) { Key 0x20 }       # PSN: continue offline
WaitDraws 1000 (120 * $Slow); Wait 4
Key 0x20; Wait 5                  # Zagraj w gre
1..8 | ForEach-Object { Key 0x32; Wait 1 }; Wait 2
Key 0x20; Wait 6                  # Wyzwania jednoosobowe
if ($Challenge -gt 0) { 1..$Challenge | ForEach-Object { Key 0x32; Wait 1 }; Wait 1 }
if ($Shots) { & $ctl shot (Join-Path $shotDir 'challenge-list.png') | Out-Null }
Key 0x20; Wait 26                 # the chosen challenge
Key 0x20; Wait 6                  # join
Key 0x20; Wait 5                  # Wczytaj profil
Key 0x20; Wait 5                  # Adam
Key 0x20; Wait 5                  # Gotow
if ((Title) -match 'draws: (\d+)' -and [int]$Matches[1] -lt 600) { Key 0x20 }   # missed press (red is no answer)
$t0 = Get-Date
if ($Shots -or $capTrigger) { Wait 30 }
if ($Shots) { & $ctl shot (Join-Path $shotDir 'round.png') | Out-Null }
if ($capTrigger) { foreach ($n in 1..3) { Set-Content $capTrigger "$n"; Start-Sleep 2 } }
if ($marker) { Start-Sleep 33; New-Item -ItemType File -Path $marker | Out-Null }
$answers = @('0x31','0x32','0x33','0x34'); $n = 0; $nextShot = 60
while (((Get-Date) - $t0).TotalSeconds -lt $RoundSeconds) {
    Start-Sleep 1
    $elapsed = ((Get-Date) - $t0).TotalSeconds
    if ($AnswerEvery -gt 0 -and [int]$elapsed % $AnswerEvery -eq 0) { Key $answers[$n % 4]; $n++ }
    if ($Shots -and $elapsed -ge $nextShot) { & $ctl shot (Join-Path $shotDir ("round_{0:000}.png" -f $nextShot)) | Out-Null; $nextShot += 60 }
}
"title at end: $(Title)"
Stop-Process -Id $procId -Force
"log=$log"
