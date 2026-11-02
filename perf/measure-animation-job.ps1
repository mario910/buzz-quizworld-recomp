# Whole image-40 job 0/1/1/0; qualified image-41 host pipeline stays enabled.
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^build-vulkan-[A-Za-z0-9_-]+$')][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag,
    [Parameter(Mandatory=$true)][string]$SaveTemplate,
    [ValidateSet('Menu','Round')][string]$Scene='Menu',
    [ValidateSet('Fps','Cpu')][string]$Metric='Fps',
    [string]$CpuBinding=''
)
$ErrorActionPreference='Stop'
$animationRoot=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $animationRoot
try {
    & ./tests/scripts/assert-geometry-build-idle.ps1
    if(($Metric -eq 'Cpu') -ne [bool]$CpuBinding){throw 'Only CPU series require a CPU binding'}
    $animationOut=Join-Path $animationRoot "analysis/native-geometry/phase4/$Tag"
    if(Test-Path -LiteralPath $animationOut){throw 'Use a fresh series tag'}
    $animationExe=Join-Path $animationRoot "$BuildDirectory/BuzzQuizWorldRecomp.exe"
    $animationMap=Join-Path $animationRoot "$BuildDirectory/BuzzQuizWorldRecomp.map"
    $animationManifest=Join-Path $animationRoot "$BuildDirectory/game-build.json"
    $animationBinding=Get-Content -LiteralPath $animationManifest -Raw | ConvertFrom-Json
    if($animationBinding.renderer -ne 'vulkan' -or (Get-FileHash $animationExe).Hash.ToLowerInvariant() -ne $animationBinding.exe_sha256){throw 'Invalid frozen EXE/renderer manifest'}
    $animationTemplate=(Resolve-Path -LiteralPath $SaveTemplate).Path
    $animationSaveRelative='dev_hdd0/home/00000001/savedata/BCES00647/SAVEDATA'
    $animationTemplateSave=Join-Path $animationTemplate $animationSaveRelative
    $animationPins=@{}
    foreach($animationPath in @($animationExe,$animationMap,$animationManifest,$animationTemplateSave,$PSCommandPath,(Join-Path $PSScriptRoot 'measure-vulkan-scene.ps1'),(Join-Path $PSScriptRoot 'measure-process-work.py'),(Join-Path $PSScriptRoot 'frame-readback.py'),(Join-Path $animationRoot 'port/native_animation_runtime.cpp'))){$animationPins[$animationPath]=(Get-FileHash -LiteralPath $animationPath).Hash.ToLowerInvariant()}
    if($CpuBinding){
        $CpuBinding=(Resolve-Path -LiteralPath $CpuBinding).Path
        $animationPins[$CpuBinding]=(Get-FileHash $CpuBinding).Hash.ToLowerInvariant()
        $animationCpuBinding=Get-Content $CpuBinding -Raw | ConvertFrom-Json
        if($animationCpuBinding.exe_sha256 -ne $animationPins[$animationExe] -or $animationCpuBinding.map_sha256 -ne $animationPins[$animationMap]){throw 'CPU binding differs from frozen EXE/map'}
    }
    New-Item -ItemType Directory -Path $animationOut | Out-Null
    $animationWindow=if($Scene -eq 'Menu'){@(40,98)}else{@(200,270)}
    $animationProgress=Join-Path $animationOut 'progress.jsonl'
    $animationIndex=0
    foreach($animationArm in @(0,1,1,0)){
        & ./tests/scripts/assert-geometry-build-idle.ps1
        foreach($animationPath in $animationPins.Keys){if((Get-FileHash -LiteralPath $animationPath).Hash.ToLowerInvariant() -ne $animationPins[$animationPath]){throw "Measurement input changed: $animationPath"}}
        foreach($animationKey in @(Get-ChildItem Env: | Where-Object {$_.Name -match '^BUZZ_(GEOMETRY|ANIMATION|REPLAY|UNBUFFERED)|^SPU_INTERP_|^PS3RECOMP_VK_DUMP|^BUZZ_(TEST_INPUT_FILE|SCRIPT)$'} | ForEach-Object Name)){Remove-Item "Env:$animationKey"}
        $animationIndex++
        $animationRunTag="$Tag-$animationIndex-$animationArm"
        $animationSave=Join-Path $animationOut "save-$animationIndex"
        Copy-Item -LiteralPath $animationTemplate -Destination $animationSave -Recurse
        if((Get-FileHash (Join-Path $animationSave $animationSaveRelative)).Hash.ToLowerInvariant() -ne $animationPins[$animationTemplateSave]){throw 'Copied save differs'}
        $animationSettings=[ordered]@{BUZZ_ANIMATION_JOB=$animationArm;BUZZ_GEOMETRY_JOB=1;BUZZ_GEOMETRY_JOB_HOST_PIPELINE=1;BUZZ_GEOMETRY_JOB_PUBLICATION=1;BUZZ_GEOMETRY_JOB_COMMANDS=1;BUZZ_GEOMETRY_JOB_LEGACY=0;BUZZ_GEOMETRY_JOB_AUDIT=0;BUZZ_GEOMETRY_DEBUG_STAGES=0;BUZZ_GEOMETRY=1;BUZZ_GEOMETRY_KERNELS=827;BUZZ_STAGEF=1;BUZZ_IMAGE40_LOCALS=0;SPU_INTERP_EXACT=0;SPU_INTERP_STATS=0;SPU_INTERP_ENTRIES=0}
        $animationEnvironment=(@($animationSettings.Keys | ForEach-Object {"$_=$($animationSettings[$_])"}) -join ';')+";PS3_GAMEDATA_ROOT=$animationSave"
        $animationRecord=[ordered]@{tag=$animationRunTag;index=$animationIndex;arm=$animationArm;animation_job=$animationArm;geometry_job=1;scene=$Scene;metric=$Metric;renderer='vulkan';window_seconds=$animationWindow;exe_sha256=$animationPins[$animationExe];map_sha256=$animationPins[$animationMap];manifest_sha256=$animationPins[$animationManifest];save_sha256=$animationPins[$animationTemplateSave];source_pins=$animationPins;settings=$animationSettings;round_intro_seconds=50;content_capture_ms=$(if($Scene -eq 'Round'){190000}else{0});content_shot_at=$(if($Scene -eq 'Round'){195}else{0});cpu_binding_sha256=$(if($CpuBinding){$animationPins[$CpuBinding]}else{''})}
        [ordered]@{state='start';run=$animationRecord} | ConvertTo-Json -Depth 6 -Compress | Add-Content -LiteralPath $animationProgress -Encoding utf8
        $animationArgs=@{Scene=$Scene;Tag=$animationRunTag;Seconds=100;RoundSeconds=140;RoundIntroSeconds=50;BuildDirectory=$BuildDirectory;Env=$animationEnvironment;PhaseLog=(Join-Path $animationOut "$animationRunTag.phases.jsonl")}
        if($Scene -eq 'Round'){$animationArgs.ContentReadbackAtMs=190000;$animationArgs.ContentShotAt=195}
        $animationCpuPath=Join-Path $animationOut "$animationRunTag.cpu.json"
        if($CpuBinding){$animationArgs.CpuBinding=$CpuBinding;$animationArgs.CpuOutput=$animationCpuPath;$animationArgs.CpuWindow=$animationWindow}
        Write-Output "START $animationRunTag $Scene $Metric"
        & ./perf/measure-vulkan-scene.ps1 @animationArgs *> (Join-Path $animationOut "$animationRunTag.runner.log")
        $animationCsv=Join-Path $animationRoot "analysis/vk-scene-$animationRunTag.csv"
        $animationLog=Join-Path $animationRoot "analysis/vk-scene-$animationRunTag.stderr.log"
        $animationText=Get-Content -LiteralPath $animationLog -Raw
        foreach($animationMarker in @('window present mode mailbox','two-context asynchronous draw batches enabled','profile=host-groups publication=1 commands=1','host pipeline=1 ')){if(-not $animationText.Contains($animationMarker)){throw "Missing runtime marker $animationMarker"}}
        if($animationArm -and -not $animationText.Contains('[animation-job] mode=1 ')){throw 'Native animation mode missing'}
        if($animationArm -and -not $animationText.Contains('[animation-job/host-pipeline] native 256 (owner count)')){throw 'Actual native animation coverage missing'}
        if(-not $animationArm -and $animationText.Contains('[animation-job/host-pipeline]')){throw 'Disabled animation path executed natively'}
        if($animationText -cmatch '\[null-call\]|\] DIFF|MISMATCH\b|SPU HALT|ACCESS_VIOLATION|animation-job/native-outer'){throw 'Game error or paired checker in performance run'}
        $animationRows=Import-Csv -LiteralPath $animationCsv
        if($animationRows.Count -lt 500 -or [double]$animationRows[-1].elapsed_ms -lt 1000*$animationWindow[1]){throw 'Incomplete measurement window'}
        [ordered]@{state='done';run=$animationRecord;csv_sha256=(Get-FileHash $animationCsv).Hash.ToLowerInvariant();log_sha256=(Get-FileHash $animationLog).Hash.ToLowerInvariant();cpu_sha256=$(if($CpuBinding){(Get-FileHash $animationCpuPath).Hash.ToLowerInvariant()}else{''})} | ConvertTo-Json -Depth 6 -Compress | Add-Content -LiteralPath $animationProgress -Encoding utf8
        Write-Output "DONE $animationRunTag"
    }
} finally {Pop-Location}
