param([Parameter(Mandatory=$true)][string]$Corpus,[string]$EvidenceDirectory='')
$ErrorActionPreference='Stop'
$animationRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $animationRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    $animationPhase=[IO.Path]::GetFullPath((Join-Path $animationRoot 'analysis/native-geometry/phase4'))
    $animationRun=if($EvidenceDirectory){[IO.Path]::GetFullPath((Join-Path $animationRoot $EvidenceDirectory))}else{Join-Path $animationPhase ('matrix-check-'+[guid]::NewGuid().ToString('N'))}
    if(-not $animationRun.StartsWith($animationPhase+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Matrix evidence must be inside this workspace phase4 directory'}
    if(Test-Path -LiteralPath $animationRun){throw 'Matrix evidence already exists; use a fresh directory'}
    New-Item -ItemType Directory -Path $animationRun | Out-Null
    & ./toolenv/Scripts/python.exe tools/codegen/generate-animation-matrices-test.py
    if($LASTEXITCODE){throw 'Animation matrix oracle generation failed'}
    $animationVs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $animationVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $animationCompiler=& ./toolenv/Scripts/python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $animationCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /DSPU_NATIVE_RTZ=1 /DSPU_RTZ_RESULT_GUARD=1 /clang:-mfma /clang:-frounding-math /clang:-ffp-contract=off /Ips3recomp/include /Ips3recomp/runtime/spu /Ithird_party/zlib /Ibuild-vulkan-windows/zlib tests/animation-host-matrices-test.cpp "/Fo$animationRun/test.obj" "/Fe$animationRun/test.exe" /link build-vulkan-windows/zlib/libzs.lib
    if($LASTEXITCODE){throw 'Animation matrix test build failed'}
    & (Join-Path $animationRun 'test.exe') (Resolve-Path -LiteralPath $Corpus).Path 2>&1 | Tee-Object -FilePath (Join-Path $animationRun 'test.log')
    if($LASTEXITCODE){throw 'Animation matrix test failed'}
    Write-Output "Matrix verification evidence: $animationRun"
} finally {Pop-Location}
