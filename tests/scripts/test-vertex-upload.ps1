$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    foreach($case in @('vertex-upload-test','vertex-reflection-test','vertex-upload-gpu-test','timer-short-spin-test')) {
        & $compiler /nologo /O2 /W0 "tests/$case.c" "/Foanalysis/$case.obj" "/Feanalysis/$case.exe" /link d3dcompiler.lib dxguid.lib d3d12.lib dxgi.lib
        if($LASTEXITCODE){throw "Vertex test build failed: $case"}
        & ".\analysis\$case.exe"
        if($LASTEXITCODE){throw "Vertex test failed: $case"}
        if($case -eq 'vertex-upload-gpu-test') {
            & ".\analysis\$case.exe" --warp
            if($LASTEXITCODE){throw "Vertex WARP test failed"}
        }
    }
    foreach($case in @('vertex-cache-test','vertex-cmp-test','vertex-fetch-plan-test')) {
        & $compiler /nologo /O2 /W0 /Ips3recomp/include "tests/$case.c" ps3recomp/libs/video/rsx_vertex_fetch.c "/Foanalysis/" "/Feanalysis/$case.exe"
        if($LASTEXITCODE){throw "Vertex fetch build failed: $case"}
        & ".\analysis\$case.exe"
        if($LASTEXITCODE){throw "Vertex fetch test failed: $case"}
    }
} finally {Pop-Location}
