$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    $common=@('/nologo','/O2','/W0','/Ips3recomp/include','/Ips3recomp/runtime/spu','/Ips3recomp/runtime/platform','/Foanalysis/')
    $sources=@('spu_drain.c','spu_lockstep.c','spu_coherency.c','spu_workload.c','spurs_policy.c','spurs_job.c','spurs_job_ls.c','spu_tsp_weak.c','spu_vm_pagemap.c','spurs_policy_blob_weak.c','spu_interp.c') | ForEach-Object {"ps3recomp/runtime/spu/$_"}
    & $compiler @common ps3recomp/runtime/spu/tests/test_spu_mfc_sharded.c @sources ps3recomp/runtime/platform/win32_compat.c /Feanalysis/mfc-directory-test.exe
    if($LASTEXITCODE){throw 'MFC directory test build failed'}
    & .\analysis\mfc-directory-test.exe
    if($LASTEXITCODE){throw 'MFC directory tests failed'}
    & $compiler @common ps3recomp/runtime/spu/tests/test_spu_mfc_shared_lookup.c @sources ps3recomp/runtime/platform/win32_compat.c /Feanalysis/mfc-shared-lookup-test.exe
    if($LASTEXITCODE){throw 'MFC shared lookup build failed'}
    & .\analysis\mfc-shared-lookup-test.exe
    if($LASTEXITCODE){throw 'MFC shared lookup test failed'}
    & .\analysis\mfc-directory-test.exe 0
    if($LASTEXITCODE){throw 'MFC control lifetime benchmark failed'}
    & .\analysis\mfc-directory-test.exe 1
    if($LASTEXITCODE){throw 'MFC sharded lifetime benchmark failed'}
    & $compiler @common tests/log-sample-test.c /Feanalysis/log-sample-test.exe
    if($LASTEXITCODE){throw 'Log sampler build failed'}
    & .\analysis\log-sample-test.exe
    if($LASTEXITCODE){throw 'Log sampler test failed'}
} finally {Pop-Location}
