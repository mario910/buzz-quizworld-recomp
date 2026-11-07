$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location $root
try {
    $vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /W0 /O2 /Ips3recomp/include /Ips3recomp/runtime/spu tests/native-function-check-test.cpp /Foanalysis/native-function-check-test.obj /Feanalysis/native-function-check-test.exe
    if ($LASTEXITCODE) { throw 'Function-check test build failed' }
    & .\analysis\native-function-check-test.exe
    if ($LASTEXITCODE) { throw 'Function-check test failed' }
    & (Join-Path $compilerDirectory 'clang-cl.exe') /nologo /W0 /O2 /std:c++17 /Ips3recomp/include /Ips3recomp/runtime/spu tests/stagef-image39-return-test.cpp /Foanalysis/stagef-image39-return-test.obj /Feanalysis/stagef-image39-return-test.exe
    if ($LASTEXITCODE) { throw 'Stage F return test build failed' }
    & .\analysis\stagef-image39-return-test.exe
    if ($LASTEXITCODE) { throw 'Stage F return test failed' }
} finally { Pop-Location }
