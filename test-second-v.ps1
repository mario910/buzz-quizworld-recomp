$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
$savedSecondV=[Environment]::GetEnvironmentVariable('GCM_SECOND_V','Process')
try {
    $vsRoot=& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $compilerDirectory=& .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $compiler=Join-Path $compilerDirectory 'clang-cl.exe'
    & $compiler /nologo /O2 /W0 /TC /c /Ips3recomp/include /Ips3recomp/libs/video tests/gcm-cpu-link-stubs.c /Foanalysis/gcm-cpu-link-stubs.obj
    if($LASTEXITCODE){throw 'GCM CPU link fixture build failed'}
    & $compiler /nologo /O2 /W0 /TC /c /Gy /Gw /Ips3recomp/include /Ips3recomp/libs/video ps3recomp/libs/video/tests/test_gcm_second_v.c /Foanalysis/gcm-second-v-test.obj
    if($LASTEXITCODE){throw 'Second-V CPU test build failed'}
    & $compiler /nologo analysis/gcm-second-v-test.obj analysis/gcm-cpu-link-stubs.obj /Feanalysis/gcm-second-v-test.exe /link /OPT:REF
    if($LASTEXITCODE){throw 'Second-V CPU test link failed'}
    foreach($mode in @('1','0')) {
        $env:GCM_SECOND_V=$mode
        & .\analysis\gcm-second-v-test.exe
        if($LASTEXITCODE){throw "Second-V CPU test failed in mode $mode"}
    }
} finally {
    if($null -eq $savedSecondV){Remove-Item Env:GCM_SECOND_V -ErrorAction SilentlyContinue}
    else {[Environment]::SetEnvironmentVariable('GCM_SECOND_V',$savedSecondV,'Process')}
    Pop-Location
}
