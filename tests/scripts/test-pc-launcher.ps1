$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
Push-Location $projectRoot
$oracleBefore=[Environment]::GetEnvironmentVariable('BUZZ_CONFIG_ORACLE','Process')
$adapterBefore=[Environment]::GetEnvironmentVariable('BUZZ_INPUT_ADAPTER_CHECK','Process')
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 /UNDEBUG /std:c++17 tests/launcher-config-oracle.cpp port/buzzer_input_config.cpp /Foanalysis/ /Feanalysis/launcher-config-oracle.exe
    if($LASTEXITCODE){throw 'Native config oracle build failed'}
    $env:BUZZ_CONFIG_ORACLE=Join-Path $projectRoot 'analysis\launcher-config-oracle.exe'
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 /UNDEBUG /std:c++17 tests/launcher-input-adapter-test.cpp port/buzzer_input_config.cpp port/buzzer_input_win32.cpp /Foanalysis/ /Feanalysis/launcher-input-adapter-test.exe /link user32.lib
    if($LASTEXITCODE){throw 'Native input adapter check build failed'}
    $env:BUZZ_INPUT_ADAPTER_CHECK=Join-Path $projectRoot 'analysis\launcher-input-adapter-test.exe'
    & .\toolenv\Scripts\python.exe -m unittest discover -s tests -p 'test_pc_launcher*.py' -v
    if($LASTEXITCODE){throw 'PC launcher tests failed'}
    & .\scripts\Launch-Buzz.ps1 -Smoke
    if(-not $?){throw 'Withdrawn UI smoke failed'}
} finally {
    if($null -eq $oracleBefore){Remove-Item Env:BUZZ_CONFIG_ORACLE -ErrorAction SilentlyContinue}
    else{[Environment]::SetEnvironmentVariable('BUZZ_CONFIG_ORACLE',$oracleBefore,'Process')}
    if($null -eq $adapterBefore){Remove-Item Env:BUZZ_INPUT_ADAPTER_CHECK -ErrorAction SilentlyContinue}
    else{[Environment]::SetEnvironmentVariable('BUZZ_INPUT_ADAPTER_CHECK',$adapterBefore,'Process')}
    Pop-Location
}
