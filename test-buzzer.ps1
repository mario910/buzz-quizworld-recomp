$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    foreach($testName in @('buzzer-protocol-test','buzzer-script-test','tls-template-test')){
        & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 /UNDEBUG /std:c++17 "tests/$testName.cpp" "/Foanalysis/$testName.obj" "/Feanalysis/$testName.exe"
        if($LASTEXITCODE){throw "$testName build failed"}
        & ".\analysis\$testName.exe"
        if($LASTEXITCODE){throw "$testName failed"}
    }
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 /UNDEBUG /std:c++17 tests/buzzer-input-test.cpp port/buzzer_input_config.cpp /Foanalysis/ /Feanalysis/buzzer-input-test.exe
    if($LASTEXITCODE){throw 'buzzer-input-test build failed'}
    & .\analysis\buzzer-input-test.exe
    if($LASTEXITCODE){throw 'buzzer-input-test failed'}
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /O2 /UNDEBUG /std:c++17 /Iport/recompiled /DPPU_NO_INLINE_LOADS tests/virtual-buzzer-test.cpp port/virtual_buzzer.cpp /Foanalysis/ /Feanalysis/virtual-buzzer-test.exe /link user32.lib
    if($LASTEXITCODE){throw 'virtual-buzzer-test build failed'}
    & .\analysis\virtual-buzzer-test.exe
    if($LASTEXITCODE){throw 'virtual-buzzer-test failed'}
} finally {Pop-Location}
