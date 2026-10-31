$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
$geometryPreviousMode = $env:BUZZ_GEOMETRY
$geometryPreviousMask = $env:BUZZ_GEOMETRY_KERNELS
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /O2 /UNDEBUG /W0 /Ips3recomp/include /Ips3recomp/runtime/spu /DBUZZ_GEOMETRY_AVAILABLE=1 tests/geometry-runtime-test.cpp port/native_geometry.cpp /Foanalysis/native-geometry/phase2/ /Feanalysis/native-geometry/phase2/runtime-test.exe
    if ($LASTEXITCODE) { throw 'Geometry controller test build failed' }
    $env:BUZZ_GEOMETRY = '2'; $env:BUZZ_GEOMETRY_KERNELS = '1'
    & .\analysis\native-geometry\phase2\runtime-test.exe
    if ($LASTEXITCODE) { throw 'Geometry controller test failed' }
} finally {
    $env:BUZZ_GEOMETRY = $geometryPreviousMode; $env:BUZZ_GEOMETRY_KERNELS = $geometryPreviousMask
    Pop-Location
}
