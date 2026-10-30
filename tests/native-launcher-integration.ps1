# Local integration only. All mutations are in a new ignored test workspace.
# No keyboard/focus automation; bounded launch stops only its own child game.
param([ValidateRange(0,300)][int]$Seconds=30,[string]$PcSettings='',[switch]$ExpectMenu,
    [ValidateSet('build-windows','build-vulkan-windows')][string]$BuildDirectory='build-windows',[switch]$ExpectVulkan,
    [switch]$SkipIntro,[string]$UpdateContent='', [string]$GuestButtons='')
$ErrorActionPreference='Stop'
if($ExpectMenu -and ($env:BUZZ_PC_MENU_TEST -ne '1' -or $Seconds -lt 17)){throw 'Menu integration requires BUZZ_PC_MENU_TEST=1 and at least 17 test seconds'}
if($SkipIntro -and -not $Seconds){throw 'SkipIntro requires a bounded game launch'}
$buttonEvents=@()
if($GuestButtons) {
    $previous=-1.0
    foreach($buttonCommand in $GuestButtons.Split(',')) {
        if($buttonCommand -notmatch '^(\d{1,3}(?:\.\d{1,3})?):(?:0x)?([0-9a-fA-F]{1,5})$'){throw 'GuestButtons expects seconds:hex-mask, separated by commas'}
        $second=[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)
        $mask=[Convert]::ToUInt32($Matches[2],16)
        if($second -lt 0 -or $second -ge $Seconds -or $second-$previous -lt 0.5 -or -not $mask -or $buttonEvents.Count -ge 64){throw 'GuestButtons needs ordered nonzero pulses, at least 0.5s apart, inside the bounded test'}
        $buttonEvents+=,[pscustomobject]@{Second=$second;Mask=$mask}
        $previous=$second
    }
}
$root=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $root 'build-launcher\BuzzLauncher.exe'
$caseRoot=Join-Path $root ('analysis\native-launcher-check-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $caseRoot 'game') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'buzz-paths.json') -Destination (Join-Path $caseRoot 'buzz-paths.json')
Copy-Item -LiteralPath (Join-Path $root ($BuildDirectory+'\BuzzQuizWorldRecomp.exe')) -Destination (Join-Path $caseRoot 'game\BuzzQuizWorldRecomp.exe')
$manifest=Join-Path $caseRoot 'game\game-build.json'
Copy-Item -LiteralPath (Join-Path $root ($BuildDirectory+'\game-build.json')) -Destination $manifest
$original=Get-Content -LiteralPath $manifest -Raw
$data=$original | ConvertFrom-Json
if($UpdateContent) {
    if($data.family -ne 'quizworld-102-verified'){throw 'Explicit update content is only supported for the verified EU 1.02 fixture'}
    $content=(Resolve-Path -LiteralPath $UpdateContent).Path
    if(-not (Test-Path -LiteralPath $content -PathType Container) -or (Split-Path $content -Leaf) -ne 'Patched'){throw 'UpdateContent must name the prepared update Patched folder, not a save/runtime tree'}
    $destination=Join-Path $caseRoot 'runtime\dev_hdd0\game\BCES00647\USRDIR\Patched'
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach($language in @('GLOBAL','CRO','CZE','GRC','POL','RUS')) {
        foreach($suffix in @('.EDAT','SCRIPTS.SDAT')) {
            Copy-Item -LiteralPath (Join-Path $content ($language+$suffix)) -Destination $destination
        }
    }
    Write-Output 'Copied explicit EU 1.02 Patched assets only; no profiles, saves or plaintext caches.'
}
function Run-Check([string]$name,[int]$expected) {
    $p=Start-Process -FilePath $exe -ArgumentList ('--project "'+$caseRoot+'" --check') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $caseRoot ($name+'.out.log')) -RedirectStandardError (Join-Path $caseRoot ($name+'.err.log'))
    if($p.ExitCode -ne $expected){throw "Check $name returned $($p.ExitCode), expected $expected"}
}
Run-Check 'valid' 0
$data=$original | ConvertFrom-Json
$data.exe_sha256='0'*64
$data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding UTF8
Run-Check 'bad-exe-hash' 1
$data=$original | ConvertFrom-Json
$data.family='wrong-adapter-family'
$data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding UTF8
Run-Check 'bad-family' 1
$data=$original | ConvertFrom-Json
$data.runtime_defaults=[pscustomobject]@{unexpected_option='2'}
$data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifest -Encoding UTF8
Run-Check 'bad-runtime-option' 1
Copy-Item -LiteralPath (Join-Path $root ($BuildDirectory+'\game-build.json')) -Destination $manifest -Force
if($Seconds) {
    if($PcSettings) {
        New-Item -ItemType Directory -Path (Join-Path $caseRoot 'runtime') -Force | Out-Null
        $PcSettings | Set-Content -LiteralPath (Join-Path $caseRoot 'runtime\pc-settings.ini') -Encoding UTF8
    }
    $testOptions=if($SkipIntro){' --test-skip-intro'}else{''}
    $mailbox=Join-Path $caseRoot 'guest-buttons.txt'
    $mailboxBefore=$env:BUZZ_TEST_INPUT_FILE
    try {
        if($buttonEvents.Count){
            [IO.File]::WriteAllText($mailbox,"0:0`n",[Text.UTF8Encoding]::new($false))
            $env:BUZZ_TEST_INPUT_FILE=$mailbox
        }
        $p=Start-Process -FilePath $exe -ArgumentList ('--project "'+$caseRoot+'" --launch --test-seconds '+$Seconds+$testOptions) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $caseRoot 'launch.out.log') -RedirectStandardError (Join-Path $caseRoot 'launch.err.log')
    } finally {$env:BUZZ_TEST_INPUT_FILE=$mailboxBefore}
    $sawGame=$false
    $buttonIndex=0;$guestClock=$null
    $titles=[System.Collections.Generic.HashSet[string]]::new()
    while(-not $p.WaitForExit(500)) {
        foreach($child in (Get-CimInstance Win32_Process -Filter ('ParentProcessId='+$p.Id))) {
            if($child.Name -ne 'BuzzQuizWorldRecomp.exe'){throw 'Unexpected launcher subprocess (expected only the native game)'}
            $sawGame=$true
            if(-not $guestClock){$guestClock=[Diagnostics.Stopwatch]::StartNew()}
            $gameProcess=Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue
            if($gameProcess.MainWindowTitle){[void]$titles.Add($gameProcess.MainWindowTitle)}
        }
        if($buttonIndex -lt $buttonEvents.Count -and $guestClock -and $guestClock.Elapsed.TotalSeconds -ge $buttonEvents[$buttonIndex].Second){
            # Explicit diagnostic USB pulses only; never native desktop input.
            $mask=$buttonEvents[$buttonIndex].Mask;$buttonIndex++
            [IO.File]::WriteAllText($mailbox,("{0}:{1:X}`n" -f $buttonIndex,$mask),[Text.UTF8Encoding]::new($false))
        }
    }
    if($p.ExitCode -ne 0 -or -not $sawGame){throw 'Bounded direct game launch failed'}
    if($buttonIndex -ne $buttonEvents.Count){throw 'Game stopped before the requested internal guest pulses completed'}
    $sessions=Get-ChildItem -LiteralPath (Join-Path $caseRoot 'runtime\launcher\sessions') -Directory
    if(@($sessions).Count -ne 1){throw 'Expected one isolated native launch session'}
    $log=Get-Content -LiteralPath (Join-Path $sessions[0].FullName 'stderr.log') -Raw
    if($log -match 'Unhandled exception|ACCESS_VIOLATION'){throw 'Native game reported an exception'}
    if($PcSettings -and $log -notmatch '\[pc-settings\] configured=1'){throw 'Runtime PC settings were not loaded'}
    if($SkipIntro -and $log -notmatch '\[buzz-video\] Startup-only video skip active'){throw 'Requested diagnostic intro skip was not applied'}
    if($ExpectVulkan -and ($log -notmatch '\[rsx\] Vulkan backend init OK' -or $log -notmatch '\[RSX vulkan\] window swapchain' -or $log -notmatch '\[RSX vulkan\] register-file draw engine path')){throw 'Vulkan did not initialize real windowed guest rendering'}
    if($env:BUZZ_PHYSICAL_BUZZERS -ne '0' -and $log -notmatch '\[buzz-hid\] RawInput ready'){throw 'Physical buzzer RawInput did not initialize in the real game'}
    if($ExpectMenu) {
        if($log -notmatch '\[pc-menu\] D3D12 overlay initialized' -or $log -match '\[pc-menu\].*FAILED'){throw 'Overlay initialization failed'}
        if(([regex]::Matches($log,'\[pc-menu\] opened')).Count -ne 2 -or ([regex]::Matches($log,'\[pc-menu\] closed')).Count -ne 2){throw 'Escape did not open/close the menu exactly twice'}
        if($log -notmatch '\[pc-menu\] test: keyboard close/reopen and controller page completed'){throw 'Internal menu navigation did not complete'}
        $saved=Get-Content -LiteralPath (Join-Path $caseRoot 'runtime\pc-settings.ini') -Raw
        $savedInput=Get-Content -LiteralPath (Join-Path $caseRoot 'runtime\input-settings.ini') -Raw
        if($saved -notmatch '(?m)^volume=0\s*$' -or $saved -notmatch '(?m)^fps_limit=30\s*$'){throw 'Menu did not persist expected live settings'}
        if($savedInput -notmatch '(?m)^gamepad.1=1,RB,LEFT,DOWN,RIGHT,UP\s*$'){throw 'R1 + clockwise D-pad preset was not preserved'}
        if($savedInput -notmatch '(?m)^pulse_ms=260\s*$'){throw 'In-game controller change was not saved'}
        $dry=Start-Process -FilePath $exe -ArgumentList ('--project "'+$caseRoot+'" --dry-run') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $caseRoot 'after-menu-dry-run.log') -RedirectStandardError (Join-Path $caseRoot 'after-menu-dry-run.err.log')
        if($dry.ExitCode -ne 0){throw 'Native launcher could not reload in-game settings'}
        $dryOutput=Get-Content -LiteralPath (Join-Path $caseRoot 'after-menu-dry-run.log') -Raw
        $jsonStart=$dryOutput.IndexOf('{')
        if($jsonStart -lt 0){throw 'Missing native launch plan after menu save'}
        $plan=$dryOutput.Substring($jsonStart) | ConvertFrom-Json
        if($plan.preferences.volume -ne 0 -or $plan.preferences.fps_limit -ne 30 -or $plan.input_ini -notmatch '(?m)^pulse_ms=260\s*$'){throw 'Launcher and in-game menu disagree on persisted preferences'}
        Write-Output 'PASS: real D3D12 menu draw, live cap, atomic saves, Escape close/reopen and controller page navigation.'
    }
    foreach($title in $titles){Write-Output ('Observed game title: '+$title)}
    if($PcSettings -match '(?m)^fps_limit=30\s*$') {
        $rates=@($titles | ForEach-Object {if($_ -match 'FPS: ([0-9.]+)'){[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)}})
        if(-not $rates.Count -or @($rates | Where-Object {$_ -gt 30.8}).Count){throw 'Observed presentation rate exceeded the 30 FPS cap'}
        if(-not @($rates | Where-Object {$_ -ge 28.5}).Count){throw 'Cap test did not reach a warm near-30 FPS sample'}
    }
    if($PcSettings -match '(?m)^fullscreen=1\s*$' -and $log -notmatch '\[host-controls\] borderless-fullscreen=1'){throw 'Borderless fullscreen request did not reach the backend'}
    if($ExpectMenu) {
        $menuEnvironmentBefore=$env:BUZZ_PC_MENU_TEST
        try {
            $env:BUZZ_PC_MENU_TEST='0'
            $restarted=Start-Process -FilePath $exe -ArgumentList ('--project "'+$caseRoot+'" --launch --test-seconds 7') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $caseRoot 'restart.out.log') -RedirectStandardError (Join-Path $caseRoot 'restart.err.log')
            if($restarted.ExitCode -ne 0){throw 'Restart after menu save failed'}
        } finally {$env:BUZZ_PC_MENU_TEST=$menuEnvironmentBefore}
        $allSessions=@(Get-ChildItem -LiteralPath (Join-Path $caseRoot 'runtime\launcher\sessions') -Directory)
        if($allSessions.Count -ne 2){throw 'Expected one original and one restarted session'}
        $newSession=$allSessions | Where-Object {$_.Name -ne $sessions[0].Name}
        $restartLog=Get-Content -LiteralPath (Join-Path $newSession.FullName 'stderr.log') -Raw
        if($restartLog -notmatch '\[pc-settings\] configured=1 volume=0% cap=30' -or $restartLog -notmatch '\[input\] loaded in-game controller preferences'){throw 'Saved audio/video and controller settings were not loaded after restart'}
        if($restartLog -match 'Unhandled exception|ACCESS_VIOLATION|\[pc-menu\] opened'){throw 'Restarted game crashed or unexpectedly opened the test menu'}
        Write-Output 'PASS: menu audio/video and controller preferences survived a real game restart.'
    }
    Write-Output 'PASS: game launched directly; no Python/PowerShell child; isolated runtime; bounded child stopped.'
}
Write-Output ('PASS: executable hash, adapter family and runtime-option guards. Artifacts: '+$caseRoot)
