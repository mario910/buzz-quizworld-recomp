$ErrorActionPreference='Stop'
$exactRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $exactRoot
$exactKeys=@('SPU_INTERP_EXACT','SPU_INTERP_STATS','SPU_INTERP_ENTRIES','SPU_STEPCAP','YDKJ_SPU_TRACE')
$exactPrevious=@{}
foreach($key in $exactKeys){$exactPrevious[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
try{
    & "$PSScriptRoot/assert-geometry-build-idle.ps1"
    $vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $ccDir=& ./toolenv/Scripts/python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir;print(get_platform_binary_dir())'
    $out='analysis/native-geometry/phase3/interpreter-exact-v1-tests'
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    & (Join-Path $ccDir 'clang-cl.exe') /nologo /std:c17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /Ips3recomp/include /Ips3recomp/runtime /Ips3recomp/runtime/spu /Ips3recomp/runtime/platform tests/spu-interpreter-exact-test.c "/Fo$out/fixture.obj" "/Fe$out/fixture.exe" /link "/MAP:$out/fixture.map"
    if($LASTEXITCODE){throw 'Exact interpreter counter fixture build failed'}
    foreach($key in $exactKeys){Remove-Item "Env:$key" -ErrorAction SilentlyContinue}
    $env:SPU_INTERP_STATS='0';$env:SPU_INTERP_ENTRIES='0'
    foreach($mode in @('unset','0','1')){
        if($mode -eq 'unset'){Remove-Item Env:SPU_INTERP_EXACT -ErrorAction SilentlyContinue}else{$env:SPU_INTERP_EXACT=$mode}
        & "./$out/fixture.exe" run "$out/state-$mode.bin" *> "$out/run-$mode.log"
        if($LASTEXITCODE){throw "Exact interpreter counter mode$mode failed"}
    }
    & ./toolenv/Scripts/python.exe -c 'from pathlib import Path;p=Path("analysis/native-geometry/phase3/interpreter-exact-v1-tests");a=(p/"state-unset.bin").read_bytes();assert a==(p/"state-0.bin").read_bytes()==(p/"state-1.bin").read_bytes();print("PASS complete context/GPR/LS/control/channel bytes identical with observer disabled/enabled")'
    if($LASTEXITCODE){throw 'Observer changed interpreter state'}
    $env:SPU_INTERP_EXACT='1'
    foreach($case in @('startup-race','full','overflow')){
        & "./$out/fixture.exe" $case *> "$out/$case.log"
        if($LASTEXITCODE){throw "Exact interpreter counter $case failed"}
    }
    $env:SPU_STEPCAP='1';$env:YDKJ_SPU_TRACE='1'
    & "./$out/fixture.exe" cap *> "$out/cap.log"
    if($LASTEXITCODE){throw 'Exact interpreter counter step-cap failed'}
    Remove-Item Env:SPU_STEPCAP,Env:YDKJ_SPU_TRACE
    & ./toolenv/Scripts/python.exe tests/check_spu_interpreter_exact_process.py --exe "$out/fixture.exe" --map "$out/fixture.map" --output-root $out
    if($LASTEXITCODE){throw 'Exact interpreter process observer failed'}
    foreach($value in @('00','2','x','1x')){
        $env:SPU_INTERP_EXACT=$value
        & "./$out/fixture.exe" invalid *> "$out/invalid-$value.log"
        if($LASTEXITCODE -ne 3 -or -not (Select-String -LiteralPath "$out/invalid-$value.log" -SimpleMatch '[spu-interp-exact] invalid')){throw "Invalid exact observer setting accepted:$value"}
    }
    $global:LASTEXITCODE=0
    'PASS exact interpreter counters:state neutrality/actual dispatch boundaries/800000 concurrent updates/first-arm race/full inventory/overflow/strict config'
}finally{
    foreach($key in $exactKeys){[Environment]::SetEnvironmentVariable($key,$exactPrevious[$key],'Process')}
    Pop-Location
}
