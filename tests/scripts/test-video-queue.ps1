$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    & $compiler /nologo /std:c++20 /EHsc /O2 /W0 /Iport/recompiled /Ips3recomp/runtime/ppu /Ips3recomp/include tests/video-queue-hle-test.cpp /Foanalysis/video-queue-hle-test.obj /Feanalysis/video-queue-hle-test.exe
    if($LASTEXITCODE){throw 'Video queue HLE fixture build failed'}
    & .\analysis\video-queue-hle-test.exe
    if($LASTEXITCODE){throw 'Video queue HLE fixture failed'}
} finally {Pop-Location}
