$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $root
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    & $compiler /nologo /O2 /UNDEBUG /EHsc /std:c++20 /Iport/recompiled /Ips3recomp/include tests/frame-limit-test.cpp port/frame_limit.cpp ps3recomp/libs/system/host_controls.c /Foanalysis/ /Feanalysis/frame-limit-test.exe
    if($LASTEXITCODE){throw 'Frame limiter test build failed'}
    & .\analysis\frame-limit-test.exe
    if($LASTEXITCODE){throw 'Frame limiter test failed'}
    & $compiler /nologo /O2 /UNDEBUG /EHsc /std:c++20 /Ips3recomp/include tests/host-controls-test.cpp ps3recomp/libs/system/host_controls.c /Foanalysis/ /Feanalysis/host-controls-test.exe
    if($LASTEXITCODE){throw 'Host controls test build failed'}
    & .\analysis\host-controls-test.exe
    if($LASTEXITCODE){throw 'Host controls test failed'}
} finally {Pop-Location}
