param([switch]$ResultGuardFP)
$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference='Stop'
Push-Location $projectRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    $common=@('/nologo','/O2','/W0','/Ips3recomp/include','/Ips3recomp/runtime/spu','-mavx2','-mfma','/clang:-frounding-math','/clang:-ffp-contract=off')
    if($ResultGuardFP){$common+='/DSPU_RTZ_RESULT_GUARD'}
    & $compiler @common /DSPU_NATIVE_RTZ /DSPU_RTZ_LEGACY_FIX_ABI /DRTZ_ABI_NAME=rtz_abi_legacy /c tests/rtz-abi-kernel.c /Foanalysis/rtz-abi-legacy.obj
    if($LASTEXITCODE){throw 'Legacy ABI build failed'}
    & $compiler @common /DSPU_NATIVE_RTZ /DRTZ_ABI_NAME=rtz_abi_vector /c tests/rtz-abi-kernel.c /Foanalysis/rtz-abi-vector.obj
    if($LASTEXITCODE){throw 'Vector ABI build failed'}
    & $compiler @common /c tests/rtz-abi-test.c /Foanalysis/rtz-abi-test.obj
    if($LASTEXITCODE){throw 'ABI fixture compilation failed'}
    & $compiler /nologo analysis/rtz-abi-test.obj analysis/rtz-abi-legacy.obj analysis/rtz-abi-vector.obj /Feanalysis/rtz-abi-test.exe
    if($LASTEXITCODE){throw 'ABI fixture build failed'}
    & .\analysis\rtz-abi-test.exe
    if($LASTEXITCODE){throw 'ABI fixture failed'}
} finally {Pop-Location}
