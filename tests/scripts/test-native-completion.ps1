$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $toolPython=Join-Path $(if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $projectRoot 'toolenv'}) 'Scripts/python.exe'
    & $toolPython tools/codegen/generate-native-completion.py
    if($LASTEXITCODE){throw 'Completion generator failed'}
    $profile=Get-Content analysis/native-completion/profile.json -Raw | ConvertFrom-Json
    $flags=@('/nologo','/O2','/UNDEBUG','/std:c++17','/Iport/recompiled')
    if($profile.version -eq '100'){$flags+='/DBUZZ_COMPLETION_DISC100=1'}
    if($profile.version -eq 'us100'){$flags+='/DBUZZ_COMPLETION_US100=1'}
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $compilerDirectory=& $toolPython -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $compilerDirectory 'clang-cl.exe') @flags tests/native-completion-test.cpp /Foanalysis/native-completion-test.obj /Feanalysis/native-completion-test.exe
    if($LASTEXITCODE){throw 'Completion test build failed'}
    & .\analysis\native-completion-test.exe
    if($LASTEXITCODE){throw 'Completion equivalence failed'}
} finally {Pop-Location}
