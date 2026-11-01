$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
$geometryPreviousAudit = $env:BUZZ_GEOMETRY_JOB_AUDIT
$geometryPreviousMax = $env:BUZZ_GEOMETRY_JOB_AUDIT_MAX
$geometryPreviousJob = $env:BUZZ_GEOMETRY_JOB
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    New-Item -ItemType Directory -Path analysis/native-geometry/phase3 -Force | Out-Null
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /O2 /UNDEBUG /W0 /Ips3recomp/include /Ips3recomp/runtime/spu /DBUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE=1 /DBUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE=1 tests/geometry-job-runtime-test.cpp port/native_geometry_job.cpp /Foanalysis/native-geometry/phase3/ /Feanalysis/native-geometry/phase3/job-runtime-test.exe
    if ($LASTEXITCODE) { throw 'Whole-driver controller build failed' }
    $env:BUZZ_GEOMETRY_JOB = '0'; $env:BUZZ_GEOMETRY_JOB_AUDIT = '1'; $env:BUZZ_GEOMETRY_JOB_AUDIT_MAX = '0'
    foreach ($geometryCase in @('pass','external_ls','async','unknown_reference','direct_reference','callback_reference','unstable_exit','post_exit_input','unstable_queue','post_exit_queue','first_event','first_reservation','first_queue','bad_event_before','bad_reservation_before','bad_write','missing_channel','extra_channel','bad_exit','bad_register','bad_control','unknown_shadow','direct_shadow','callback_shadow')) {
        $geometryLog = "analysis/native-geometry/phase3/runtime-$geometryCase.log"
        & .\analysis\native-geometry\phase3\job-runtime-test.exe $geometryCase *> $geometryLog
        $geometryExpected = if ($geometryCase -in @('pass','external_ls','async','unknown_reference','direct_reference','callback_reference','unstable_exit','post_exit_input','unstable_queue','post_exit_queue','first_event','first_reservation','first_queue')) { 0 } else { 73 }
        if ($LASTEXITCODE -ne $geometryExpected) { throw "Controller $geometryCase returned $LASTEXITCODE, expected $geometryExpected" }
        if ($geometryExpected -eq 73 -and -not (Select-String -LiteralPath $geometryLog -SimpleMatch '[geometry-job-audit] DIFF')) { throw 'Missing mismatch diagnostic' }
        "Controller $geometryCase PASS"
    }
    $env:BUZZ_GEOMETRY_JOB_AUDIT = '0'; $env:BUZZ_GEOMETRY_JOB = '2'
    foreach ($geometryCase in @('pass','unsupported','external_ls','async','unknown_reference','direct_reference','callback_reference','unstable_exit','post_exit_input','unstable_queue','post_exit_queue','first_event','first_reservation','first_queue','bad_event_before','bad_reservation_before','bad_write','missing_channel','extra_channel','bad_exit','bad_register','bad_control','unknown_shadow','direct_shadow','callback_shadow')) {
        $geometryLog = "analysis/native-geometry/phase3/runtime-native2-$geometryCase.log"
        & .\analysis\native-geometry\phase3\job-runtime-test.exe $geometryCase *> $geometryLog
        $geometryExpected = if ($geometryCase -in @('pass','unsupported','external_ls','async','unknown_reference','direct_reference','callback_reference','unstable_exit','post_exit_input','unstable_queue','post_exit_queue','first_event','first_reservation','first_queue')) { 0 } else { 73 }
        if ($LASTEXITCODE -ne $geometryExpected) { throw "Native controller $geometryCase returned $LASTEXITCODE, expected $geometryExpected" }
        if ($geometryExpected -eq 73 -and -not (Select-String -LiteralPath $geometryLog -SimpleMatch '[geometry-job-audit] DIFF')) { throw 'Missing native mismatch diagnostic' }
        "Native checked controller $geometryCase PASS"
    }
    $env:BUZZ_GEOMETRY_JOB = '1'
    & .\analysis\native-geometry\phase3\job-runtime-test.exe pass *> analysis/native-geometry/phase3/runtime-native1-pass.log
    if ($LASTEXITCODE) { throw 'Actual-native controller failed' }
    'Native actual controller PASS'
    & .\analysis\native-geometry\phase3\job-runtime-test.exe direct_reference *> analysis/native-geometry/phase3/runtime-native1-direct_reference.log
    if ($LASTEXITCODE) { throw 'Actual-native direct callback did not execute exactly once' }
    'Native actual direct callback PASS'
    & .\analysis\native-geometry\phase3\job-runtime-test.exe callback_reference *> analysis/native-geometry/phase3/runtime-native1-callback_reference.log
    if ($LASTEXITCODE) { throw 'Actual-native callback boundary changed dispatch or repeated effects' }
    'Native actual callback boundary PASS'


    $env:BUZZ_GEOMETRY_JOB = '0'
    & .\analysis\native-geometry\phase3\job-runtime-test.exe off *> analysis/native-geometry/phase3/runtime-off.log
    if ($LASTEXITCODE) { throw 'Disabled native controller failed' }
    'Disabled controller PASS'
    $env:BUZZ_GEOMETRY_JOB_AUDIT = '0'
    & .\analysis\native-geometry\phase3\job-runtime-test.exe fallback_flush *> analysis/native-geometry/phase3/runtime-first-fallback-flush.log
    if ($LASTEXITCODE -or -not (Select-String -LiteralPath analysis/native-geometry/phase3/runtime-first-fallback-flush.log -SimpleMatch '[geometry-job/rigid-skin] first-fallback=1 native=0')) { throw 'First fallback was not durably logged before abrupt exit' }
    'First fallback persisted before abrupt exit PASS'
    $geometryOptionCases = 0
    foreach ($geometryOption in @('PUBLISH','LAYOUT','VERTEX','COMMANDS','COMMAND_PUBLISH','EMIT','FULL_COMMANDS','ATTRIBUTES','CODECS','DESCRIPTORS','UNIT_VECTORS','HALF_STREAM','FLOAT32_STREAM','FIXED_STREAM','SNORM_STREAM','I16_STREAM','CMP_STREAM','U8_INTEGER_STREAM','BLEND_SCATTER','BLEND_LISTS','RUN_LISTS','CUSTOM_STREAM','SKIN_DISPATCH','WEIGHTED_SKIN','SKIN_DIRECTIONS','SKIN_FLOW','RIGID_SKIN','SKIN_COFACTOR','SKIN_NORMALIZED','INDEX_DELTAS','INDEX_UNPACK','INDEX_EXPAND','INDEX_RELOCATE','INDEX_TRIANGLES','INDEX_CONTROL','TRANSFORM_FINISH','VERTEX_PREPARE','TRANSFORM_CONTROL','ALTERNATE_CULL','RING','FLOAT_OUTPUT','HALF_OUTPUT','CMP_OUTPUT','SNORM_OUTPUT','UNORM_OUTPUT','I16_OUTPUT','U8_INTEGER_OUTPUT','FORMATTER','CONVERSION_CONTROL')) {
        $geometryOptionKey = 'BUZZ_GEOMETRY_JOB_' + $geometryOption
        $geometryOptionPrevious = [Environment]::GetEnvironmentVariable($geometryOptionKey, 'Process')
        try {
            foreach ($geometryValue in @('unset','0','1','2','3','00','1x','x')) {
                if ($geometryValue -eq 'unset') { Remove-Item "Env:$geometryOptionKey" -ErrorAction SilentlyContinue }
                else { Set-Item "Env:$geometryOptionKey" $geometryValue }
                $geometryValid = $geometryValue -in @('unset','0','1') -or ($geometryOption -in @('FLOAT32_STREAM','FIXED_STREAM','SNORM_STREAM','I16_STREAM','CMP_STREAM','U8_INTEGER_STREAM','BLEND_SCATTER','RUN_LISTS','CUSTOM_STREAM','SKIN_DISPATCH','WEIGHTED_SKIN','SKIN_DIRECTIONS','SKIN_FLOW','RIGID_SKIN','SKIN_COFACTOR','SKIN_NORMALIZED','INDEX_DELTAS','INDEX_UNPACK','INDEX_EXPAND','INDEX_RELOCATE','INDEX_TRIANGLES','INDEX_CONTROL','TRANSFORM_FINISH','VERTEX_PREPARE','TRANSFORM_CONTROL','ALTERNATE_CULL','FLOAT_OUTPUT','HALF_OUTPUT','CMP_OUTPUT','SNORM_OUTPUT','UNORM_OUTPUT','I16_OUTPUT','U8_INTEGER_OUTPUT','FORMATTER','CONVERSION_CONTROL') -and $geometryValue -eq '2')
                $geometryWant = if (-not $geometryValid) { 'reject' } elseif ($geometryValue -eq 'unset') { '0' } else { $geometryValue }
                $geometryConfigLog = "analysis/native-geometry/phase3/runtime-option-$geometryOption-$geometryValue.log"
                & .\analysis\native-geometry\phase3\job-runtime-test.exe option $geometryOption $geometryWant *> $geometryConfigLog
                $geometryCode = if ($geometryValid) { 0 } else { 75 }
                if ($LASTEXITCODE -ne $geometryCode) { throw "Option $geometryOption=$geometryValue returned $LASTEXITCODE expected $geometryCode" }
                $geometryDiagnostic = if ($geometryValid) { '[config] accepted PASS' } else { '[config] rejected' }
                if (-not (Select-String -LiteralPath $geometryConfigLog -SimpleMatch $geometryDiagnostic)) { throw "Missing option diagnostic: $geometryConfigLog" }
                $geometryOptionCases++
            }
        } finally {
            if ($null -eq $geometryOptionPrevious) { Remove-Item "Env:$geometryOptionKey" -ErrorAction SilentlyContinue }
            else { Set-Item "Env:$geometryOptionKey" $geometryOptionPrevious }
        }
    }
    "Configuration options: $geometryOptionCases production-getter cases PASS"
    $global:LASTEXITCODE = 0

} finally {
    $env:BUZZ_GEOMETRY_JOB = $geometryPreviousJob
    $env:BUZZ_GEOMETRY_JOB_AUDIT = $geometryPreviousAudit
    $env:BUZZ_GEOMETRY_JOB_AUDIT_MAX = $geometryPreviousMax
    Pop-Location
}
