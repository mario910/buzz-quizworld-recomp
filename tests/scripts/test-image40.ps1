# Standalone arithmetic and actual execution-owner regressions (Windows/clang).
param([switch]$ResultGuardFP)
$projectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
Push-Location $projectRoot
try {
    $vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler = Join-Path $compilerDirectory 'clang-cl.exe'
    $common = @('/nologo','/O2','/W0','/Ips3recomp/include','/Ips3recomp/runtime/spu','/Ips3recomp/runtime/platform','/Foanalysis/')
    if($ResultGuardFP){$common+='/DSPU_RTZ_RESULT_GUARD'}
    & $compiler @common -mavx2 -mfma /clang:-frounding-math /clang:-ffp-contract=off tests/image40-fp-test.c /Feanalysis/image40-fp-test.exe
    if ($LASTEXITCODE) { throw 'Arithmetic test build failed' }
    & .\analysis\image40-fp-test.exe
    if ($LASTEXITCODE) { throw 'Arithmetic test failed' }
    & $compiler @common -mavx2 -mfma /DTEST_IMAGE39_FP /clang:-frounding-math /clang:-ffp-contract=off tests/image40-fp-test.c /Feanalysis/image39-fp-test.exe
    if ($LASTEXITCODE) { throw 'Image 39 arithmetic test build failed' }
    & .\analysis\image39-fp-test.exe
    if ($LASTEXITCODE) { throw 'Image 39 arithmetic test failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-image39-native.py
    if ($LASTEXITCODE) { throw 'Image 39 generation failed' }
    & .\toolenv\Scripts\python.exe tools/codegen/generate-image40-native.py
    if ($LASTEXITCODE) { throw 'Image 40 generation failed' }
    & $compiler @common -mavx2 -mfma /clang:-frounding-math /clang:-ffp-contract=off tests/image40-locals-test.c /Feanalysis/image40-locals-test.exe
    if ($LASTEXITCODE) { throw 'Local-register test build failed' }
    & .\analysis\image40-locals-test.exe
    if ($LASTEXITCODE) { throw 'Local-register test failed' }
    & $compiler @common tests/d3d12-sampler-test.c /Feanalysis/d3d12-sampler-test.exe
    if ($LASTEXITCODE) { throw 'Sampler test build failed' }
    & .\analysis\d3d12-sampler-test.exe
    if ($LASTEXITCODE) { throw 'Sampler test failed' }
    $sources = @('spu_drain.c','spu_lockstep.c','spu_coherency.c','spu_workload.c',
        'spurs_policy.c','spurs_job.c','spurs_job_ls.c','spu_tsp_weak.c',
        'spu_vm_pagemap.c','spurs_policy_blob_weak.c','spu_interp.c') |
        ForEach-Object { "ps3recomp/runtime/spu/$_" }
    & $compiler @common /DSPU_NATIVE_RTZ_IMAGE=41 /DSPU_NATIVE_RTZ_EXTRA_IMAGE=40 /DSPU_NATIVE_RTZ_IMAGE39=39 tests/spurs-rtz-owner-test.c @sources ps3recomp/runtime/platform/win32_compat.c /Feanalysis/image40-owner-test.exe /link Synchronization.lib
    if ($LASTEXITCODE) { throw 'Owner test build failed' }
    & .\analysis\image40-owner-test.exe
    if ($LASTEXITCODE) { throw 'Owner test failed' }
} finally {
    Pop-Location
}
