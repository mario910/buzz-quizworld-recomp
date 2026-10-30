# Matched frame timing, no captures or desktop input. Private settings leave
# the player's saved preferences intact; mute affects device output only.
param([ValidateSet(0,30)][int]$Limit=30,[switch]$LiveSwitches,
      [ValidateSet('Language','MainMenu')][string]$Scene='MainMenu',
      [ValidateRange(30,3600)][int]$Seconds=120,
      [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Tag=(Get-Date -Format 'yyyyMMdd-HHmmss'),
      [ValidateSet('build-vulkan-windows','build-windows')][string]$BuildDirectory='build-vulkan-windows')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
$keys=@('BUZZ_PC_SETTINGS','BUZZ_RENDER_PERF','BUZZ_SCRIPT','BUZZ_PC_MENU_TEST','BUZZ_PC_MENU_TEST_DELAY_MS')
$previous=@{}
foreach($key in $keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
try {
    $prefix=Join-Path $root "analysis/frame-limit-$Tag"
    foreach($suffix in @('.ini','.csv','.log','.json')){if(Test-Path -LiteralPath ($prefix+$suffix)){throw "Measurement already exists: $prefix$suffix"}}
    [IO.Directory]::CreateDirectory((Join-Path $root 'analysis')) | Out-Null
    [IO.File]::WriteAllText(($prefix+'.ini'),"version=1`nvolume=100`nfps_limit=$Limit`nfullscreen=0`noffline=1`nrenderer=auto`n")
    $env:BUZZ_PC_SETTINGS=$prefix+'.ini'
    $env:BUZZ_RENDER_PERF=$prefix+'.csv'
    if($Scene -eq 'MainMenu'){$env:BUZZ_SCRIPT='12:0x01,25:0x01'}else{Remove-Item Env:BUZZ_SCRIPT -ErrorAction SilentlyContinue}
    if($LiveSwitches){$env:BUZZ_PC_MENU_TEST='frame-limit'}else{Remove-Item Env:BUZZ_PC_MENU_TEST -ErrorAction SilentlyContinue}
    $env:BUZZ_PC_MENU_TEST_DELAY_MS='0'
    $exe=Join-Path $root "$BuildDirectory/BuzzQuizWorldRecomp.exe"
    @{renderer=$BuildDirectory;scene=$Scene;limit=$Limit;live_switches=$LiveSwitches.IsPresent;seconds=$Seconds;
      exe_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLower();
      git_head=(& git rev-parse HEAD);git_status=(& git status --short);
      cpu=(Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name);
      gpu=(Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name)} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ($prefix+'.json')
    & .\scripts\run-windows.ps1 -Seconds $Seconds -MenuBootstrap -SkipVideo -Mute -Background -BuildDirectory $BuildDirectory |
        Tee-Object -FilePath ($prefix+'.log')
    if($LASTEXITCODE){throw 'Frame-limit measurement failed'}
    Write-Output "Frame timings: $prefix.csv (exclude boot/transitions; live mode alternates 0/30 every 15 seconds)"
} finally {
    foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}
    Pop-Location
}
