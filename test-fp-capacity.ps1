$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    foreach($test in @('fp-constant-abi','fp-program-capacity')) {
        & $compiler /nologo /O2 /W0 /Ips3recomp/include "tests/$test-test.c" ps3recomp/libs/video/rsx_fp_decompiler.c /Foanalysis/ "/Feanalysis/$test-test.exe"
        if($LASTEXITCODE){throw "Build failed: $test"}
        & ".\analysis\$test-test.exe"
        if($LASTEXITCODE){throw "Test failed: $test"}
    }
    & $compiler /nologo /O2 /W0 tests/fp-constant-layout-windows-test.cpp /Foanalysis/ /Feanalysis/fp-constant-layout-windows-test.exe /link d3dcompiler.lib dxguid.lib
    if($LASTEXITCODE){throw 'Reflection test build failed'}
    & .\analysis\fp-constant-layout-windows-test.exe
    if($LASTEXITCODE){throw 'Reflection test failed'}
} finally {Pop-Location}
