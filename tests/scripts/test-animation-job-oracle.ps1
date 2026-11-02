param([Parameter(Mandatory=$true)][string]$Corpus,[string]$EvidenceDirectory='',
    [ValidateSet('Compare','Replace','Both')][string]$Mode='Both')
$ErrorActionPreference='Stop'
$animationRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $animationRoot
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    $animationPhase=[IO.Path]::GetFullPath((Join-Path $animationRoot 'analysis/native-geometry/phase4'))
    $animationRun=if($EvidenceDirectory){[IO.Path]::GetFullPath((Join-Path $animationRoot $EvidenceDirectory))}else{Join-Path $animationPhase ('job-oracle-check-'+[guid]::NewGuid().ToString('N'))}
    if(-not $animationRun.StartsWith($animationPhase+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Animation evidence must be inside this workspace phase4 directory'}
    if(Test-Path -LiteralPath $animationRun){throw 'Animation evidence already exists'}
    New-Item -ItemType Directory -Path $animationRun | Out-Null
    & ./toolenv/Scripts/python.exe tools/codegen/generate-animation-job-oracle.py
    if($LASTEXITCODE){throw 'Animation job oracle generation failed'}
    $animationVs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
    & (Join-Path $animationVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $animationCompiler=& ./toolenv/Scripts/python.exe -c 'from clang_tool_chain.wrapper import get_platform_binary_dir; print(get_platform_binary_dir())'
    & (Join-Path $animationCompiler 'clang-cl.exe') /nologo /std:c++17 /MD /O2 /UNDEBUG /W0 /arch:AVX2 /DSPU_NATIVE_RTZ=1 /DSPU_RTZ_RESULT_GUARD=1 /clang:-mfma /clang:-frounding-math /clang:-ffp-contract=off /Ips3recomp/include /Ips3recomp/runtime/spu /Ithird_party/zlib /Ibuild-vulkan-windows/zlib tests/animation-job-oracle-test.cpp "/Fo$animationRun/test.obj" "/Fe$animationRun/test.exe" /link build-vulkan-windows/zlib/libzs.lib
    if($LASTEXITCODE){throw 'Animation job oracle build failed'}
    $animationModes=if($Mode -eq 'Both'){@('Compare','Replace')}else{@($Mode)}
    foreach($animationMode in $animationModes) {
        $animationCase=Join-Path $animationRun $animationMode
        New-Item -ItemType Directory -Path $animationCase | Out-Null
        & (Join-Path $animationRun 'test.exe') (Resolve-Path -LiteralPath $Corpus).Path $animationCase $animationMode 2>&1 | Tee-Object -FilePath (Join-Path $animationCase 'test.log')
        if($LASTEXITCODE){throw "Animation job oracle $animationMode failed"}
    }
    if($Mode -eq 'Both') {
        foreach($animationFaultMode in @('Corrupt','CorruptVector','CorruptMatrix','CorruptAffine','CorruptBlend','CorruptHierarchy','CorruptExtraPose','CorruptExtraCurve','CorruptScalarKeys','CorruptClipTime')) {
        $animationCase=Join-Path $animationRun $animationFaultMode
        New-Item -ItemType Directory -Path $animationCase | Out-Null
        # Isolate the expected failing process so its exit status does not
        # leave LASTEXITCODE nonzero for a successful calling test driver.
        $animationArguments='"'+(Resolve-Path -LiteralPath $Corpus).Path+'" "'+$animationCase+'" '+$animationFaultMode
        $animationFault=Start-Process -FilePath (Join-Path $animationRun 'test.exe') -ArgumentList $animationArguments -WorkingDirectory $animationRoot -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $animationCase 'stdout.log') -RedirectStandardError (Join-Path $animationCase 'stderr.log')
        Get-Content -LiteralPath (Join-Path $animationCase 'stdout.log'),(Join-Path $animationCase 'stderr.log') | Set-Content -LiteralPath (Join-Path $animationCase 'test.log') -Encoding utf8
        $animationFailure=$animationFault.ExitCode
        if(-not $animationFailure -or -not (Select-String -LiteralPath (Join-Path $animationCase 'test.log') -SimpleMatch 'published bytes mismatch')){throw "Injected $animationFaultMode corruption was not rejected at published job output"}
        @{exit_code=$animationFailure;expected_failure=$true;reason="Injected $animationFaultMode sign fault rejected by full published-byte comparison"} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $animationCase 'expected-failure.json') -Encoding utf8
        Write-Output "PASS injected native $animationFaultMode corruption rejected at job publication"
        }
    }
    Write-Output "Animation job oracle evidence: $animationRun"
} finally {Pop-Location}
