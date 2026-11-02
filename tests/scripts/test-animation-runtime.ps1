param(
    [Parameter(Mandatory=$true)][ValidatePattern('^build-vulkan-[A-Za-z0-9_-]+$')][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
$ErrorActionPreference='Stop'
$animationRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location -LiteralPath $animationRoot
$animationPrevious=@{}
try {
    & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
    $animationPhase=[IO.Path]::GetFullPath((Join-Path $animationRoot 'analysis/native-geometry/phase4'))
    $animationOut=[IO.Path]::GetFullPath((Join-Path $animationRoot $EvidenceDirectory))
    if(-not $animationOut.StartsWith($animationPhase+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Runtime evidence must be a private phase4 subdirectory'}
    if(Test-Path -LiteralPath $animationOut){throw 'Runtime evidence already exists'}
    $animationExe=(Resolve-Path "$BuildDirectory/BuzzQuizWorldRecomp.exe").Path
    $animationManifest=Get-Content "$BuildDirectory/game-build.json" -Raw | ConvertFrom-Json
    if($animationManifest.renderer -ne 'vulkan' -or (Get-FileHash $animationExe).Hash.ToLowerInvariant() -ne $animationManifest.exe_sha256){throw 'Frozen EXE/renderer binding differs'}
    . ./scripts/buzz-paths.ps1
    $animationPaths=Get-BuzzPaths
    if((Get-BuzzFileSha256 -Path $animationPaths.eboot_elf) -ne $animationManifest.elf_sha256){throw 'Wrong game executable for runtime adapter'}
    New-Item -ItemType Directory -Path $animationOut | Out-Null
    $animationKeys=@(@(Get-ChildItem Env: | Where-Object {$_.Name -match '^BUZZ_(GEOMETRY|ANIMATION|REPLAY|STAGEF|IMAGE40)|^SPURS_NATIVE_EXTRA_FP$'} | ForEach-Object Name)+@('BUZZ_ANIMATION_JOB','BUZZ_GEOMETRY_JOB','BUZZ_GEOMETRY_JOB_AUDIT','BUZZ_REPLAY_CORPUS','BUZZ_REPLAY_CONTRACT','BUZZ_STAGEF','BUZZ_IMAGE40_LOCALS','SPURS_NATIVE_EXTRA_FP') | Select-Object -Unique)
    foreach($animationKey in $animationKeys){$animationPrevious[$animationKey]=[Environment]::GetEnvironmentVariable($animationKey,'Process');Remove-Item "Env:$animationKey" -ErrorAction SilentlyContinue}
    $env:BUZZ_REPLAY_CONTRACT='1';$env:BUZZ_STAGEF='1';$env:BUZZ_IMAGE40_LOCALS='0';$env:SPURS_NATIVE_EXTRA_FP='1'
    foreach($animationArm in @(
        @{Name='image40-retained';Image=40;Job=0;Animation=0;Jobs=17714;Transfers=115162},
        @{Name='image40-native';Image=40;Job=0;Animation=1;Jobs=17714;Transfers=115162},
        @{Name='image40-checked';Image=40;Job=0;Animation=2;Jobs=17714;Transfers=115162},
        @{Name='image41-native';Image=41;Job=1;Animation=0;Jobs=19508;Transfers=58195},
        @{Name='image41-checked';Image=41;Job=2;Animation=0;Jobs=19508;Transfers=58195}
    )) {
        & (Join-Path $PSScriptRoot 'assert-geometry-build-idle.ps1')
        $env:BUZZ_REPLAY_CORPUS=(Resolve-Path "analysis/native-geometry/replay-final/all/image$($animationArm.Image)").Path
        $env:BUZZ_GEOMETRY_JOB=[string]$animationArm.Job;$env:BUZZ_ANIMATION_JOB=[string]$animationArm.Animation
        $animationLog=Join-Path $animationOut ($animationArm.Name+'.log')
        # This is a Windows GUI executable: wait on its process explicitly,
        # including the final buffered logs and native coverage summary.
        $animationProcess=Start-Process -FilePath $animationExe -ArgumentList ('"'+$animationPaths.eboot_elf+'"') -WorkingDirectory $animationRoot -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput ($animationLog+'.stdout') -RedirectStandardError ($animationLog+'.stderr')
        Get-Content -LiteralPath ($animationLog+'.stdout'),($animationLog+'.stderr') | Set-Content -LiteralPath $animationLog -Encoding utf8
        if($animationProcess.ExitCode){throw "Runtime $($animationArm.Name) failed ($($animationProcess.ExitCode)): $animationLog"}
        $animationText=Get-Content -LiteralPath $animationLog -Raw
        $animationExpected="[replay] $($animationArm.Jobs) exact, 0 with concurrent input changes, 0 FAILED ($($animationArm.Transfers) transfers)"
        if(-not $animationText.Contains($animationExpected)){throw "Incomplete corpus gate: $animationLog"}
        if($animationArm.Animation) {
            $animationExpectedCount=if($animationArm.Animation -eq 2){17714}else{0}
            $animationExpectedStats="[animation-job] attempted=17714 native=17714 compared=$animationExpectedCount fallback=0 audit-skipped=0 "
            if(-not $animationText.Contains($animationExpectedStats)){throw "Native coverage/whole-job comparisons missing: $animationLog"}
        }
        Write-Output "PASS $($animationArm.Name): $($animationArm.Jobs) exact jobs, $($animationArm.Transfers) ordered transfers"
    }
    Copy-Item -LiteralPath "$BuildDirectory/game-build.json" -Destination "$animationOut/game-build.json"
} finally {
    foreach($animationKey in $animationPrevious.Keys){if($null -eq $animationPrevious[$animationKey]){Remove-Item "Env:$animationKey" -ErrorAction SilentlyContinue}else{[Environment]::SetEnvironmentVariable($animationKey,$animationPrevious[$animationKey],'Process')}}
    Pop-Location
}
