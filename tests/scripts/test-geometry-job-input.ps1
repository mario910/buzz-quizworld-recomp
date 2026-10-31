param([string]$Corpus = '')
$ErrorActionPreference = 'Stop'
$geometryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $geometryRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    & .\toolenv\Scripts\python.exe tools/codegen/generate-geometry-job-input-test.py
    if ($LASTEXITCODE) { throw 'Input setup oracle generation failed' }
    $geometryVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $geometryVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $geometryCompiler = & .\toolenv\Scripts\python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    if (-not (Test-Path build-vulkan-windows/zlib/libzs.lib)) { throw 'Build the Vulkan target first to provide its zlib library' }
    & (Join-Path $geometryCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /Ips3recomp/include /Ips3recomp/runtime/spu /Ithird_party/zlib /Ibuild-vulkan-windows/zlib tests/geometry-job-input-test.cpp /Foanalysis/native-geometry/phase3/job-input-test.obj /Feanalysis/native-geometry/phase3/job-input-test.exe /link build-vulkan-windows/zlib/libzs.lib
    if ($LASTEXITCODE) { throw 'Native job entry test build failed' }
    $geometryLog = 'analysis/native-geometry/phase3/job-input-test.log'
    if ($Corpus) { & .\analysis\native-geometry\phase3\job-input-test.exe (Resolve-Path -LiteralPath $Corpus).Path 2>&1 | Tee-Object -FilePath $geometryLog }
    else { & .\analysis\native-geometry\phase3\job-input-test.exe 2>&1 | Tee-Object -FilePath $geometryLog }
    if ($LASTEXITCODE) { throw 'Native job entry test failed' }
} finally { Pop-Location }
