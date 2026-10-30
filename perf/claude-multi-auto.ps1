# Boot -> Standardowa gra, 2-4 players with saved profiles, then let the game
# run, for multi-player studio tests. Buzzer presses go through the test
# mailbox (no window focus needed); the game window stays in the background.
#   claude-multi-auto.ps1 [-Players 3] [-BuildDirectory build-vulkan-windows]
#     -Env 'K=V;K2=V2'  extra environment for the run
#     -RoundSeconds N   how long to keep answering once the game started
#     -Shots            screenshots of each step into claude-multi-shots
#     -Keep             leave the game running once the game has started
#     -Affinity 0xFF    CPU mask for the game (0xFF = 4C/8T, Steam Deck-like)
# Prints the title FPS sampled every 2 s while the game runs (mean, median, min).
# Saved profiles are picked as the game pre-selects them: 4 players need 4.
param([ValidateRange(2,4)][int]$Players = 3, [string]$BuildDirectory = 'build-windows',
      [string]$Env = '', [int]$RoundSeconds = 120, [switch]$Shots, [switch]$Keep,
      [string]$Affinity = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue) { throw 'Game already running.' }
$ctl = Join-Path $PSScriptRoot 'claude-gamectl.ps1'
$press = Join-Path $PSScriptRoot 'claude-press.ps1'
$shotDir = Join-Path $PSScriptRoot 'claude-multi-shots'
New-Item -ItemType Directory -Force $shotDir | Out-Null
$mailbox = Join-Path $shotDir 'mailbox.txt'
[IO.File]::WriteAllText($mailbox, "0:0`n", [Text.UTF8Encoding]::new($false))
$set = @('BUZZ_TEST_INPUT_FILE')
if ($Env) { foreach ($kv in $Env -split ';') { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v; $set += $k } }
$env:BUZZ_TEST_INPUT_FILE = $mailbox
$out = .\scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap -SkipVideo -Mute -Background -BuildDirectory $BuildDirectory | Out-String
foreach ($k in $set) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
$procId = [int]([regex]::Match($out, 'PID=(\d+)').Groups[1].Value)
$log = [regex]::Match($out, 'stderr=(\S.*?\.stderr\.log)').Groups[1].Value
if ($Affinity) { (Get-Process -Id $procId).ProcessorAffinity = [IntPtr][Convert]::ToInt64($Affinity, 16) }
function Title { (Get-Process -Id $procId).MainWindowTitle }
function Draws { if ((Title) -match 'draws: (\d+)') { [int]$Matches[1] } else { -1 } }
# One press at a time: the game drops presses of several handsets at once.
function Press([string[]]$p, [double]$gap = 3) { & $press -Mailbox $mailbox -Gap $gap @p }
function Shot([string]$name) { if ($Shots) { & $ctl shot (Join-Path $shotDir "$name.png") | Out-Null } }
function WaitDraws([int]$min, [int]$timeout) {
    $deadline = (Get-Date).AddSeconds($timeout)
    do { Start-Sleep -Milliseconds 500 } until ((Draws) -gt $min -or (Get-Date) -gt $deadline)
}
$all = 1..$Players | ForEach-Object { "P${_}r" }
WaitDraws 0 120; Start-Sleep 8
Press P1r 6                        # language
Press P1r 25                       # buzzer info
WaitDraws 1000 120; Start-Sleep 4
Press P1r 6                        # Zagraj w gre
Press P1r 6                        # Standardowa gra
Press P1r 50                       # Dwie do czterech, then the studio intro
Shot '1-join'
Press $all                         # Kto dzis gra: each handset joins
Start-Sleep 3; Shot '2-profile'
Press $all                         # Wczytac profil
Start-Sleep 3; Shot '3-kim-jestes'
Press $all                         # Kim jestes: the pre-selected profile
Start-Sleep 3; Shot '4-gotow'
Press $all                         # Gotow!
Start-Sleep 20; Shot '5-studio'
"PID=$procId log=$log"
if ($Keep) { return }
# Answer in turn (red buzzes in a fast round; the colours answer), and sample
# the title FPS between presses.
$t0 = Get-Date; $keys = 'r','b','o','g','y'; $n = 0; $fps = @()
while (((Get-Date) - $t0).TotalSeconds -lt $RoundSeconds) {
    if ((Get-Process -Id $procId -ErrorAction SilentlyContinue) -eq $null) { break }
    Press ("P{0}{1}" -f (($n % $Players) + 1), $keys[$n % 5]) 2; $n++
    if ((Title) -match 'FPS: ([\d.]+) \| draws: (\d+)' -and [int]$Matches[2] -gt 300) { $fps += [double]$Matches[1] }
    if ($Shots -and $n % 15 -eq 0) { Shot ("6-round-{0:000}" -f $n) }
}
if ($fps.Count) {
    $sorted = $fps | Sort-Object
    "FPS samples={0} mean={1:N1} median={2:N1} min={3:N1}" -f $fps.Count, ($fps | Measure-Object -Average).Average,
        $sorted[[int]($sorted.Count / 2)], $sorted[0]
}
"title at end: $(Title)"
Stop-Process -Id $procId -ErrorAction SilentlyContinue
