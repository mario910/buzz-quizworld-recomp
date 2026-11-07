param([switch]$HostCommands)
$ErrorActionPreference = 'Stop'
$geometryCase=if($HostCommands){'geometry-host-position-commands-test'}else{'geometry-position-commands-test'}
$geometryOutput=if($HostCommands){'host-position-commands-test'}else{'position-commands-test'}
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-position-commands-test.py
    if ($LASTEXITCODE) { throw 'Position command oracle generation failed' }
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /Ips3recomp/include /Ips3recomp/runtime/spu "tests/${geometryCase}.cpp" "/Foanalysis/native-geometry/phase3/${geometryOutput}.obj" "/Feanalysis/native-geometry/phase3/${geometryOutput}.exe"
    if ($LASTEXITCODE) { throw 'Native position command test build failed' }
    & "./analysis/native-geometry/phase3/${geometryOutput}.exe" 2>&1 | Tee-Object -FilePath "analysis/native-geometry/phase3/${geometryOutput}.log"
    if ($LASTEXITCODE) { throw 'Native position command test failed' }
} finally { Pop-Location }
