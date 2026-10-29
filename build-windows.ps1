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
    [switch]$ReferenceOnly,
    [switch]$SpuLsDebugHooks,
    [ValidateSet('-O2','-O3')][string]$SpuOpt = '-O2'
)
$ErrorActionPreference = 'Stop'
if($ReferenceOnly) {
    if($NativeCompletion -or $NativeImage40 -or $NativeImage39 -or $NativeImage36 -or $NativeImage7 -or $MStreamModules -or $StageF -or $NativeGeometry -or $MergeGeometry -or $FusedGeometry) {
        throw 'ReferenceOnly cannot be combined with version-specific native adapters.'
    }
    $NativeSpuFloat=[System.Management.Automation.SwitchParameter]::new($false)
}
if ($SpuIsa -notin @('avx2','avx512') -and -not $PSBoundParameters.ContainsKey('NativeSpuFloat')) {
    $NativeSpuFloat = [System.Management.Automation.SwitchParameter]::new($false)
}
$experimentRoot = $PSScriptRoot
$expectedExe = Join-Path $experimentRoot 'build-windows\BuzzQuizWorldRecomp.exe'
if (Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $expectedExe }) {
    throw 'Close this checkout''s running game before building; it will not be stopped for you.'
}
$toolEnvironment = if($env:BUZZ_TOOL_ENV){$env:BUZZ_TOOL_ENV}else{Join-Path $experimentRoot 'toolenv'}
$toolScripts = Join-Path $toolEnvironment 'Scripts'
if (-not (Test-Path -LiteralPath (Join-Path $toolScripts 'python.exe'))) { throw 'Run .\Setup-Buzz.ps1 first.' }
$env:PATH = "$toolScripts;$env:PATH"
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 Build Tools with Desktop development with C++ and a Windows SDK.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'No Visual Studio C++ toolchain found. Install the Desktop development with C++ workload.' }
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
$hostProfile = ''
$generationPath=Join-Path $experimentRoot 'analysis\game-generation.json'
if(Test-Path -LiteralPath $generationPath) {
    $generation=Get-Content -LiteralPath $generationPath -Raw | ConvertFrom-Json
    . (Join-Path $experimentRoot 'buzz-paths.ps1')
    $buildElf=(Get-BuzzPaths).eboot_elf
    if((Get-BuzzFileSha256 -Path $buildElf) -ne $generation.elf_sha256) {
        throw 'Generated translations belong to another executable. Generate in a separate preparation workspace.'
    }
    if($generation.family -in @('quizworld-eu100','quizworld-us100')) {
        $hostProfile=Join-Path $experimentRoot 'analysis\host-disc100\port'
    } elseif($generation.family -ne 'quizworld-102-verified') {throw 'Unknown generation family'}
}
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
    $completionProfile = Get-Content (Join-Path $experimentRoot 'analysis\native-completion\profile.json') -Raw | ConvertFrom-Json
    if($completionProfile.source -notin @('ppu_recomp_013.cpp','ppu_recomp_014.cpp','ppu_recomp_015.cpp')) { throw 'Unexpected completion source profile' }
    $completionOverride = Join-Path $experimentRoot ('analysis\native-completion\' + $completionProfile.source)
    if($ProducerTrace -and $completionProfile.version -ne '102') { throw 'Producer trace is only validated on 1.02.' }
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
& cmake -S $port -B $build -G Ninja "-DPS3RECOMP_DIR=$sdk" "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_CXX_COMPILER=$compiler" "-DPORT_HOST_PROFILE_DIR=$hostProfile" "-DPORT_SPU_ISA=$SpuIsa" "-DPORT_NATIVE_SPU_FLOAT=$($NativeSpuFloat.IsPresent)" "-DPORT_RTZ_RESULT_GUARD=$($ResultGuardFP.IsPresent)" "-DPORT_SPU_LS_DEBUG_HOOKS=$($SpuLsDebugHooks.IsPresent)" "-DPORT_SPU_OPT=$SpuOpt" "-DPORT_OPTIMIZE_PPU_HOT_PATHS=$($HotPpu.IsPresent)" "-DPORT_GEOMETRY_SOURCE=$geometryOverride" "-DPORT_COMPLETION_SOURCE=$completionOverride" "-DPORT_IMAGE40_SOURCE=$image40Override" "-DPORT_IMAGE39_SOURCE=$image39Override" "-DPORT_IMAGE7_SOURCE=$image7Override" "-DPORT_MSTREAM_DIR=$mstreamDir" "-DPORT_IMAGE36_SOURCE=$image36Override" "-DPORT_VIDEO_SOURCE=$videoOverride" "-DPORT_PRODUCER_PROBE_DIR=$producerOverride" -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_FLAGS_RELEASE=/O1 /DNDEBUG' '-DCMAKE_CXX_FLAGS_RELEASE=/O1 /DNDEBUG'
if ($LASTEXITCODE) { throw "CMake configuration failed: $LASTEXITCODE" }
& cmake --build $build --parallel $Jobs
if ($LASTEXITCODE) { throw "Build failed: $LASTEXITCODE" }
if($generation) {
    $runtimePlan = & (Join-Path $toolScripts 'python.exe') (Join-Path $experimentRoot 'verify-game.py') --runtime-options
    if($LASTEXITCODE){throw 'Cannot bind runtime defaults to the validated build'}
    @{family=$generation.family;elf_sha256=$generation.elf_sha256;exe_sha256=(Get-BuzzFileSha256 -Path $expectedExe);runtime_defaults=($runtimePlan | ConvertFrom-Json)} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build 'game-build.json') -Encoding UTF8
}
