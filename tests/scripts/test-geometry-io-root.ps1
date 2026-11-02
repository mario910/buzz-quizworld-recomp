$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
$geometryPreviousAudit = $env:BUZZ_GEOMETRY_JOB_AUDIT
$geometryPreviousJob = $env:BUZZ_GEOMETRY_JOB
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    New-Item -ItemType Directory -Path analysis/native-geometry/phase3 -Force | Out-Null
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /O2 /UNDEBUG /W0 /Ips3recomp/include /Ips3recomp/runtime/spu /DBUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE=1 /DBUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE=1 tests/geometry-io-root-test.cpp port/native_geometry_job.cpp /Foanalysis/native-geometry/phase3/ /Feanalysis/native-geometry/phase3/io-root-test.exe
    if ($LASTEXITCODE) { throw 'I/O-root controller build failed' }
    $env:BUZZ_GEOMETRY_JOB_AUDIT = '0'
    $geometryCases = 0
    foreach ($geometryMode in @('0','1','2')) {
        $env:BUZZ_GEOMETRY_JOB = $geometryMode
        foreach ($geometryCase in @('pass','empty','nested')) {
            & .\analysis\native-geometry\phase3\io-root-test.exe $geometryCase
            if ($LASTEXITCODE) { throw "I/O root JOB=$geometryMode $geometryCase failed: $LASTEXITCODE" }
            $geometryCases++
        }
    }
    $env:BUZZ_GEOMETRY_JOB = '2'
    foreach ($geometryCase in @('register','ls','control','pc','image','depth','policy','overlay','continuation','missing','extra','input','foreign','null_runner')) {
        $geometryLog = "analysis/native-geometry/phase3/io-root-$geometryCase.log"
        & .\analysis\native-geometry\phase3\io-root-test.exe $geometryCase *> $geometryLog
        if ($LASTEXITCODE -ne 73) { throw "I/O root $geometryCase returned $LASTEXITCODE expected 73" }
        if (-not (Select-String -LiteralPath $geometryLog -SimpleMatch '[geometry-job-audit] DIFF')) { throw "Missing root mismatch diagnostic: $geometryLog" }
        "I/O root rejects $geometryCase PASS"
        $geometryCases++
    }
    "I/O-root controller: $geometryCases cases PASS"
    $global:LASTEXITCODE = 0
} finally {
    $env:BUZZ_GEOMETRY_JOB = $geometryPreviousJob
    $env:BUZZ_GEOMETRY_JOB_AUDIT = $geometryPreviousAudit
    Pop-Location
}
