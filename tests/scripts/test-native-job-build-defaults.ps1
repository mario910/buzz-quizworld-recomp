$ErrorActionPreference='Stop'
$nativeDefaultsRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $nativeDefaultsRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    $nativeDefaultsOut='analysis/native-geometry/phase4/embedded-defaults-v1/getter-tests'
    if(Test-Path $nativeDefaultsOut){throw 'Evidence exists'}
    New-Item -ItemType Directory -Path $nativeDefaultsOut | Out-Null
    $nativeDefaultsVs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $nativeDefaultsVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $nativeDefaultsCompiler=& ./toolenv/Scripts/python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    $env:BUZZ_GEOMETRY_JOB_AUDIT='0'
    foreach($nativeDefaultsBuild in @(0,1)) {
        $nativeDefaultsCase="$nativeDefaultsOut/$nativeDefaultsBuild"
        New-Item -ItemType Directory -Path $nativeDefaultsCase | Out-Null
        & (Join-Path $nativeDefaultsCompiler 'clang-cl.exe') /nologo /std:c++17 /O2 /UNDEBUG /W0 /Ips3recomp/include /Ips3recomp/runtime/spu /DBUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE=1 /DBUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE=1 "/DBUZZ_NATIVE_JOB_DEFAULT=$nativeDefaultsBuild" tests/geometry-job-runtime-test.cpp port/native_geometry_job.cpp "/Fo$nativeDefaultsCase/" "/Fe$nativeDefaultsCase/test.exe" *> "$nativeDefaultsCase/build.log"
        if($LASTEXITCODE){throw 'Embedded default controller compile failed'}
        foreach($nativeDefaultsOverride in @('unset','0','1','2')) {
            if($nativeDefaultsOverride -eq 'unset'){Remove-Item Env:BUZZ_GEOMETRY_JOB -ErrorAction SilentlyContinue;$nativeDefaultsExpected=$nativeDefaultsBuild}
            else{$env:BUZZ_GEOMETRY_JOB=$nativeDefaultsOverride;$nativeDefaultsExpected=[int]$nativeDefaultsOverride}
            & "$nativeDefaultsCase/test.exe" build-default $nativeDefaultsExpected *> "$nativeDefaultsCase/$nativeDefaultsOverride.log"
            if($LASTEXITCODE){throw 'Embedded default / override failed'}
        }
    }
    'PASS compiled defaults 0/1 and absent/explicit 0/1/2 overrides'
} finally {Pop-Location}
