$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    $common=@('/nologo','/O2','/W0','/Ips3recomp/include','/Ips3recomp/libs/video','/Foanalysis/')
    & $compiler @common tests/zeta-layout-test.c ps3recomp/libs/video/rsx_texture_layout.c /Feanalysis/zeta-layout-windows.exe
    if($LASTEXITCODE){throw 'Zeta layout build failed'}
    & .\analysis\zeta-layout-windows.exe
    if($LASTEXITCODE){throw 'Zeta layout test failed'}
    & $compiler @common ps3recomp/libs/video/tests/test_rsx_command_texture_offset.c ps3recomp/libs/video/rsx_commands.c /Feanalysis/zeta-command-windows.exe
    if($LASTEXITCODE){throw 'Zeta commands build failed'}
    & .\analysis\zeta-command-windows.exe
    if($LASTEXITCODE){throw 'Zeta command test failed'}
    & $compiler @common tests/zeta-msaa-gpu-test.c /Feanalysis/zeta-gpu-test.exe /link d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib
    if($LASTEXITCODE){throw 'Zeta GPU build failed'}
    & .\analysis\zeta-gpu-test.exe
    if($LASTEXITCODE){throw 'Zeta GPU test failed'}
    & .\analysis\zeta-gpu-test.exe --warp
    if($LASTEXITCODE){throw 'Zeta WARP test failed'}
} finally {Pop-Location}
