param(
    [string]$CompilerDirectory = '',
    [int]$Jobs = 6,
    [ValidateSet('sse2','sse41','avx2','avx512')][string]$SpuIsa = 'avx2',
    [switch]$NativeSpuFloat = $true,
    [switch]$HotPpu = $true,
    [switch]$MergeGeometry,
    [switch]$FusedGeometry,
    [switch]$StageF,
    [switch]$NativeGeometry,
    [switch]$GeometryShufflePlans,
    [switch]$DirectGeometryLS,
    [switch]$ResultGuardFP,
    [switch]$VideoTrace,
    [switch]$ProducerTrace,
    [switch]$NativeCompletion,
    [switch]$NativeImage40,
    [switch]$NativeImage39,
    [switch]$NativeImage36,
    [switch]$NativeImage7,
    [switch]$MStreamModules,
    [switch]$SpuLsDebugHooks,
    [ValidateSet('-O2','-O3')][string]$SpuOpt = '-O2'
)
$ErrorActionPreference = 'Stop'
if ($SpuIsa -notin @('avx2','avx512') -and -not $PSBoundParameters.ContainsKey('NativeSpuFloat')) {
    $NativeSpuFloat = [System.Management.Automation.SwitchParameter]::new($false)
}
$experimentRoot = $PSScriptRoot
$toolScripts = Join-Path $experimentRoot 'toolenv\Scripts'
$env:PATH = "$toolScripts;$env:PATH"
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -property installationPath
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not $CompilerDirectory) {
    $CompilerDirectory = & (Join-Path $toolScripts 'python.exe') -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
}
$env:PATH = "$CompilerDirectory;$env:PATH"
$compiler = Join-Path $CompilerDirectory 'clang-cl.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "clang-cl missing: $compiler" }
$sdk = Join-Path $experimentRoot 'ps3recomp'
$port = Join-Path $experimentRoot 'port'
$build = Join-Path $experimentRoot 'build-windows'
$geometryOverride = ''
$completionOverride = ''
$image40Override = ''
$image39Override = ''
$videoOverride = ''
$producerOverride = ''
if ($VideoTrace) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-video-probe.py')
    if ($LASTEXITCODE) { throw 'Video probe generation failed' }
    $videoOverride = Join-Path $experimentRoot 'analysis\video-probe\ppu_recomp_019.cpp'
}
$mstreamDir = ''
if ($MStreamModules) {
    . (Join-Path $experimentRoot 'buzz-paths.ps1')
    $devFlash = (Get-BuzzPaths).dev_flash
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-mstream-modules.py') --dev-flash $devFlash
    if ($LASTEXITCODE) { throw 'MultiStream module lift failed' }
    $mstreamDir = Join-Path $experimentRoot 'analysis\mstream-modules'
}
$image7Override = ''
$image36Override = ''
if ($NativeImage36) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-image36-native.py')
    if ($LASTEXITCODE) { throw 'Image 36 generation failed' }
    $image36Override = Join-Path $experimentRoot 'analysis\native-image36\spu_recomp.c'
}
if ($NativeImage7) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-image7-native.py')
    if ($LASTEXITCODE) { throw 'Image 7 generation failed' }
    $image7Override = Join-Path $experimentRoot 'analysis\native-image7\spu_recomp.c'
}
if ($NativeImage39) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-image39-native.py')
    if ($LASTEXITCODE) { throw 'Image 39 generation failed' }
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-image39-kernels.py')
    if ($LASTEXITCODE) { throw 'Image 39 kernel wrapping failed' }
    $image39Override = Join-Path $experimentRoot 'analysis\native-image39-kernels\spu_recomp.c'
}
if ($NativeImage40) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-image40-native.py')
    if ($LASTEXITCODE) { throw 'Image 40 generation failed' }
    $image40Override = Join-Path $experimentRoot 'analysis\native-image40\spu_recomp.c'
}
if ($NativeCompletion) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-native-completion.py')
    if ($LASTEXITCODE) { throw 'Native completion generation failed' }
    $completionOverride = Join-Path $experimentRoot 'analysis\native-completion\ppu_recomp_015.cpp'
}
if ($ProducerTrace) {
    $producerArguments=@((Join-Path $experimentRoot 'generate-producer-probe.py'))
    if($completionOverride){$producerArguments+=@('--completion-source',$completionOverride)}
    & (Join-Path $toolScripts 'python.exe') @producerArguments
    if($LASTEXITCODE){throw 'Producer probe generation failed'}
    $producerOverride=Join-Path $experimentRoot 'analysis\producer-probe'
}
if ($DirectGeometryLS -and $GeometryShufflePlans) { throw 'Keep geometry experiments independent' }
if ($GeometryShufflePlans -and $SpuIsa -ne 'avx2') { throw 'Prepared geometry plans currently require the verified AVX2 build' }
if ($DirectGeometryLS -or $GeometryShufflePlans) { $NativeGeometry = [System.Management.Automation.SwitchParameter]::new($true) }
if (@($MergeGeometry,$NativeGeometry,$FusedGeometry,$StageF | Where-Object { $_ }).Count -gt 1) { throw 'Choose only one geometry experiment' }
if ($NativeGeometry) {
    $geometryArguments=@((Join-Path $experimentRoot 'generate-geometry-native.py'))
    if ($DirectGeometryLS) { $geometryArguments+='--direct-ls' }
    if ($GeometryShufflePlans) { $geometryArguments+='--shuffle-plans' }
    & (Join-Path $toolScripts 'python.exe') @geometryArguments
    if ($LASTEXITCODE) { throw "Native geometry generation failed: $LASTEXITCODE" }
    $geometryOverride = Join-Path $experimentRoot $(if($GeometryShufflePlans){'analysis\native-geometry-plans\spu_recomp.c'}elseif($DirectGeometryLS){'analysis\native-geometry-direct-ls\spu_recomp.c'}else{'analysis\native-geometry\spu_recomp.c'})
}
if ($FusedGeometry) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-geometry-fused.py')
    if ($LASTEXITCODE) { throw "Fused geometry generation failed: $LASTEXITCODE" }
    $geometryOverride = Join-Path $experimentRoot 'analysis\fused-geometry\spu_recomp.c'
}
if ($StageF) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-geometry-stagef.py')
    if ($LASTEXITCODE) { throw "Stage F geometry generation failed: $LASTEXITCODE" }
    $geometryOverride = Join-Path $experimentRoot 'analysis\stagef-geometry\spu_recomp.c'
}
if ($MergeGeometry) {
    & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'generate-geometry-merged.py')
    if ($LASTEXITCODE) { throw "Merged geometry generation failed: $LASTEXITCODE" }
    $geometryOverride = Join-Path $experimentRoot 'analysis\merged-geometry\spu_recomp.c'
}
& cmake -S $port -B $build -G Ninja "-DPS3RECOMP_DIR=$sdk" "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_CXX_COMPILER=$compiler" "-DPORT_SPU_ISA=$SpuIsa" "-DPORT_NATIVE_SPU_FLOAT=$($NativeSpuFloat.IsPresent)" "-DPORT_RTZ_RESULT_GUARD=$($ResultGuardFP.IsPresent)" "-DPORT_SPU_LS_DEBUG_HOOKS=$($SpuLsDebugHooks.IsPresent)" "-DPORT_SPU_OPT=$SpuOpt" "-DPORT_OPTIMIZE_PPU_HOT_PATHS=$($HotPpu.IsPresent)" "-DPORT_GEOMETRY_SOURCE=$geometryOverride" "-DPORT_COMPLETION_SOURCE=$completionOverride" "-DPORT_IMAGE40_SOURCE=$image40Override" "-DPORT_IMAGE39_SOURCE=$image39Override" "-DPORT_IMAGE7_SOURCE=$image7Override" "-DPORT_MSTREAM_DIR=$mstreamDir" "-DPORT_IMAGE36_SOURCE=$image36Override" "-DPORT_VIDEO_SOURCE=$videoOverride" "-DPORT_PRODUCER_PROBE_DIR=$producerOverride" -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_FLAGS_RELEASE=/O1 /DNDEBUG' '-DCMAKE_CXX_FLAGS_RELEASE=/O1 /DNDEBUG'
if ($LASTEXITCODE) { throw "CMake configuration failed: $LASTEXITCODE" }
& cmake --build $build --parallel $Jobs
if ($LASTEXITCODE) { throw "Build failed: $LASTEXITCODE" }
