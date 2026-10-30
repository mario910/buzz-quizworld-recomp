# Matched-scene frame timing for renderer work: background window, muted
# device, uncapped, private settings (the player's saved preferences are not
# touched). Buzzer input goes through the test mailbox; no focus or desktop
# input. Outputs analysis/vk-scene-<Tag>.{csv,stderr.log,ini}; summarize with
# perf/scene-window-stats.py.
#   -Scene Menu   studio main menu via BUZZ_SCRIPT; heavy camera tour from ~35 s
#   -Scene Round  first single-player challenge with the first saved profile
#                 (main menu -> Play -> 9x down -> challenges); round starts
#                 ~137 s after launch, printed as round_start_s
#   -Env 'K=V;K2=V2'  extra environment for an A/B (e.g. PS3RECOMP_ENG_DEPTH_COLOR=0)
#   -Shots        PrintWindow screenshots along the navigation, to check it
param([ValidateSet('Menu','Round')][string]$Scene = 'Menu',
      [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Tag,
      [ValidateRange(30,3600)][int]$Seconds = 100, [ValidateRange(10,3600)][int]$RoundSeconds = 120,
      [string]$Env = '', [switch]$Shots,
      [ValidateSet('build-vulkan-windows','build-windows')][string]$BuildDirectory = 'build-vulkan-windows')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
$prefix = Join-Path $root "analysis/vk-scene-$Tag"
foreach ($suffix in @('.csv','.stderr.log')) { if (Test-Path -LiteralPath ($prefix + $suffix)) { throw "Measurement already exists: $prefix$suffix" } }
[IO.Directory]::CreateDirectory((Join-Path $root 'analysis')) | Out-Null
$keys = @('BUZZ_PC_SETTINGS','BUZZ_RENDER_PERF','BUZZ_SCRIPT','BUZZ_TEST_INPUT_FILE')
$extra = @()
if ($Env) { foreach ($kv in $Env -split ';') { $extra += ($kv -split '=', 2)[0] } }
$previous = @{}
foreach ($key in $keys + $extra) { $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
$procId = 0
$exe = Join-Path $root "$BuildDirectory\BuzzQuizWorldRecomp.exe"
try {
    if ($Env) { foreach ($kv in $Env -split ';') { $k, $v = $kv -split '=', 2; [Environment]::SetEnvironmentVariable($k, $v, 'Process') } }
    [IO.File]::WriteAllText(($prefix + '.ini'), "version=1`nvolume=0`nfps_limit=0`nfullscreen=0`noffline=1`nrenderer=auto`n")
    $env:BUZZ_PC_SETTINGS = $prefix + '.ini'
    $env:BUZZ_RENDER_PERF = $prefix + '.csv'
    $mailbox = $prefix + '.mailbox.txt'
    [IO.File]::WriteAllText($mailbox, "0:0`n", [Text.UTF8Encoding]::new($false))
    $env:BUZZ_TEST_INPUT_FILE = $mailbox
    if ($Scene -eq 'Menu') { $env:BUZZ_SCRIPT = '12:0x01,25:0x01' } else { Remove-Item Env:BUZZ_SCRIPT -ErrorAction SilentlyContinue }
    $r = & .\scripts\run-windows.ps1 -Seconds 0 -MenuBootstrap -SkipVideo -Mute -Background -BuildDirectory $BuildDirectory | Out-String
    $procId = [int]([regex]::Match($r, 'PID=(\d+)').Groups[1].Value)
    $log = [regex]::Match($r, 'stderr=(\S.*?\.stderr\.log)').Groups[1].Value
    $t0 = Get-Date
    $script:seq = 0
    function Key([int]$mask) {
        $script:seq++
        [IO.File]::WriteAllText($mailbox, ("{0}:{1:x}`n" -f $script:seq, $mask), [Text.UTF8Encoding]::new($false))
        Start-Sleep -Milliseconds 300
    }
    function Shot([string]$n) { if ($Shots) { & (Join-Path $PSScriptRoot 'claude-gamectl.ps1') shot ($prefix + "-$n.png") | Out-Null } }
    function Draws { if ((Get-Process -Id $procId).MainWindowTitle -match 'draws: (\d+)') { [int]$Matches[1] } else { 0 } }
    $end = $Seconds
    if ($Scene -eq 'Round') {
        $red = 0x01; $orange = 0x08
        Start-Sleep 20
        Key $red; Start-Sleep 6                  # language
        Key $red; Start-Sleep 25                 # buzzer info -> studio menu
        Shot 'menu'
        Key $red; Start-Sleep 5                  # Play
        1..9 | ForEach-Object { Key $orange; Start-Sleep 1 }; Start-Sleep 2
        Key $red; Start-Sleep 6                  # single-player challenges
        Key $red; Start-Sleep 26                 # first challenge
        Key $red; Start-Sleep 6                  # join
        Key $red; Start-Sleep 5                  # load profile
        Key $red; Start-Sleep 5                  # first profile
        Key $red; Start-Sleep 5                  # ready
        for ($i = 0; $i -lt 4 -and (Draws) -lt 600; $i++) { Key $red; Start-Sleep 5 }
        $roundStart = ((Get-Date) - $t0).TotalSeconds
        Write-Output ("round_start_s={0:N1}" -f $roundStart)
        Shot 'round'
        $end = $roundStart + $RoundSeconds
    }
    while (((Get-Date) - $t0).TotalSeconds -lt $end) { Start-Sleep -Milliseconds 500 }
    if ($Scene -eq 'Round') { Shot 'end' }
} finally {
    if ($procId) {
        $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
        if ($p -and $p.Path -eq $exe) { Stop-Process -Id $procId -Force; Start-Sleep -Milliseconds 500 }
        if ($log -and (Test-Path -LiteralPath $log)) { Copy-Item -LiteralPath $log ($prefix + '.stderr.log') }
    }
    foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process') }
    Pop-Location
}
Write-Output "Frame timings: $prefix.csv (CSV seconds count from renderer start, not launch)"
