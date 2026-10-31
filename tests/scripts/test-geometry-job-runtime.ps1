$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
$geometryPreviousAudit = $env:BUZZ_GEOMETRY_JOB_AUDIT
$geometryPreviousMax = $env:BUZZ_GEOMETRY_JOB_AUDIT_MAX
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    New-Item -ItemType Directory -Path analysis/native-geometry/phase3 -Force | Out-Null
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /O2 /UNDEBUG /W0 /Ips3recomp/include /Ips3recomp/runtime/spu /DBUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE=1 tests/geometry-job-runtime-test.cpp port/native_geometry_job.cpp /Foanalysis/native-geometry/phase3/ /Feanalysis/native-geometry/phase3/job-runtime-test.exe
    if ($LASTEXITCODE) { throw 'Whole-driver controller build failed' }
    $env:BUZZ_GEOMETRY_JOB_AUDIT = '1'; $env:BUZZ_GEOMETRY_JOB_AUDIT_MAX = '0'
    foreach ($geometryCase in @('pass','external_ls','async','unknown_reference','unstable_exit','post_exit_input','unstable_queue','post_exit_queue','bad_write','missing_channel','extra_channel','bad_exit','bad_register','bad_control','unknown_shadow')) {
        $geometryLog = "analysis/native-geometry/phase3/runtime-$geometryCase.log"
        & .\analysis\native-geometry\phase3\job-runtime-test.exe $geometryCase *> $geometryLog
        $geometryExpected = if ($geometryCase -in @('pass','external_ls','async','unknown_reference','unstable_exit','post_exit_input','unstable_queue','post_exit_queue')) { 0 } else { 73 }
        if ($LASTEXITCODE -ne $geometryExpected) { throw "Controller $geometryCase returned $LASTEXITCODE, expected $geometryExpected" }
        if ($geometryExpected -eq 73 -and -not (Select-String -LiteralPath $geometryLog -SimpleMatch '[geometry-job-audit] DIFF')) { throw 'Missing mismatch diagnostic' }
        "Controller $geometryCase PASS"
    }
} finally {
    $env:BUZZ_GEOMETRY_JOB_AUDIT = $geometryPreviousAudit
    $env:BUZZ_GEOMETRY_JOB_AUDIT_MAX = $geometryPreviousMax
    Pop-Location
}
