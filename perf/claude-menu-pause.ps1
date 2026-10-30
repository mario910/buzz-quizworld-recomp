# Boot to the main menu in the background (muted, test mailbox input), then
# hold the PC menu open over it with the overlay's internal pause test and
# report audio peaks and title FPS/draws before, during and after.
#   claude-menu-pause.ps1 [-BuildDirectory build-vulkan-windows] [-Env 'K=V;K2=V2']
#     -MenuAt N   seconds after the first frame to open the PC menu (hold: 8 s)
param([string]$BuildDirectory = 'build-vulkan-windows', [string]$Env = '', [int]$MenuAt = 75)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue) { throw 'Game already running.' }
$press = Join-Path $PSScriptRoot 'claude-press.ps1'
$shotDir = Join-Path $PSScriptRoot 'claude-multi-shots'
New-Item -ItemType Directory -Force $shotDir | Out-Null
$mailbox = Join-Path $shotDir 'mailbox.txt'
[IO.File]::WriteAllText($mailbox, "0:0`n", [Text.UTF8Encoding]::new($false))
$set = @('BUZZ_TEST_INPUT_FILE','BUZZ_PC_MENU_TEST','BUZZ_PC_MENU_TEST_DELAY_MS','AUDIO_PEAK')
if ($Env) { foreach ($kv in $Env -split ';') { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v; $set += $k } }
$env:BUZZ_TEST_INPUT_FILE = $mailbox
$env:BUZZ_PC_MENU_TEST = 'pause'
$env:BUZZ_PC_MENU_TEST_DELAY_MS = [string]($MenuAt * 1000)
$env:AUDIO_PEAK = '1'
$out = .\scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap -SkipVideo -Mute -Background -BuildDirectory $BuildDirectory | Out-String
foreach ($k in $set) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
$procId = [int]([regex]::Match($out, 'PID=(\d+)').Groups[1].Value)
$log = [regex]::Match($out, 'stderr=(\S.*?\.stderr\.log)').Groups[1].Value
function Title { (Get-Process -Id $procId).MainWindowTitle }
function Draws { if ((Title) -match 'draws: (\d+)') { [int]$Matches[1] } else { -1 } }
$deadline = (Get-Date).AddSeconds(120)
do { Start-Sleep -Milliseconds 500 } until ((Draws) -gt 0 -or (Get-Date) -gt $deadline)
$first = Get-Date
Start-Sleep 8
& $press -Mailbox $mailbox -Gap 6 P1r          # language
& $press -Mailbox $mailbox -Gap 1 P1r          # buzzer info
while (((Get-Date) - $first).TotalSeconds -lt $MenuAt + 20) {
    "{0,5:N1}s  {1}" -f ((Get-Date) - $first).TotalSeconds, (Title)
    Start-Sleep 1
}
Stop-Process -Id $procId -ErrorAction SilentlyContinue
"log=$log"
