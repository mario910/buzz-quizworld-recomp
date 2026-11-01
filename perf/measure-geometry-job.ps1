# Four fresh same-EXE 0/1/1/0 runs. FPS and CPU are separate series.
# With Option, JOB stays1 and only that production option changes.
# Native enables the production job options in the pinned runtime source;
# baseline retains geometry mask827/StageF1 with those job options disabled.
param([Parameter(Mandatory)][ValidatePattern('^build-[A-Za-z0-9_-]+$')][string]$BuildDirectory,
      [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag,
      [Parameter(Mandatory)][string]$SaveTemplate,
      [ValidateSet('Menu','Round')][string]$Scene='Menu',
      [ValidateSet('Fps','Cpu')][string]$Metric='Fps',
      [ValidatePattern('^$|^BUZZ_GEOMETRY_JOB_[A-Z0-9_]+$')][string]$Option='',
      [string]$CpuBinding='')
$ErrorActionPreference='Stop'
$geometryRoot=Split-Path -Parent $PSScriptRoot
$geometryClearedPrevious=@{}
foreach ($geometryKey in @('BUZZ_REPLAY_CORPUS','PS3RECOMP_VK_DUMP','PS3RECOMP_VK_DUMP_EVERY','PS3RECOMP_VK_DUMP_ONCE_AT_MS')) {
    $geometryClearedPrevious[$geometryKey]=[Environment]::GetEnvironmentVariable($geometryKey,'Process')
    Remove-Item ('Env:'+$geometryKey) -ErrorAction SilentlyContinue
}
Push-Location -LiteralPath $geometryRoot
try {
    & ./tests/scripts/assert-geometry-build-idle.ps1
    if (Test-Path Env:BUZZ_REPLAY_CONTRACT) { throw 'Replay contract override forbidden' }
    if (($Metric -eq 'Cpu') -ne [bool]$CpuBinding) { throw 'Only CPU series require CpuBinding' }
    $geometryOut=Join-Path $geometryRoot "analysis/native-geometry/phase3/$Tag"
    if (Test-Path -LiteralPath $geometryOut) { throw 'Use a fresh series tag' }
    $geometryTemplate=(Resolve-Path -LiteralPath $SaveTemplate).Path
    $geometrySaveRelative='dev_hdd0/home/00000001/savedata/BCES00647/SAVEDATA'
    $geometryTemplateSave=Join-Path $geometryTemplate $geometrySaveRelative
    $geometryExe=Join-Path $geometryRoot "$BuildDirectory/BuzzQuizWorldRecomp.exe"
    $geometryMap=Join-Path $geometryRoot "$BuildDirectory/BuzzQuizWorldRecomp.map"
    $geometryHelper=Join-Path $PSScriptRoot 'measure-vulkan-scene.ps1'
    $geometryRuntime=Join-Path $geometryRoot 'port/native_geometry_job.cpp'
    $geometryRunner=$PSCommandPath
    $geometryContentTool=Join-Path $PSScriptRoot 'frame-readback.py'
    $geometryPins=@{}
    foreach ($geometryPath in @($geometryTemplateSave,$geometryExe,$geometryMap,$geometryHelper,$geometryRuntime,$geometryRunner)) {
        $geometryPins[$geometryPath]=(Get-FileHash -LiteralPath $geometryPath).Hash.ToLowerInvariant()
    }
    if ($Scene -eq 'Round') { $geometryPins[$geometryContentTool]=(Get-FileHash -LiteralPath $geometryContentTool).Hash.ToLowerInvariant() }
    $geometryCpuTool=Join-Path $PSScriptRoot 'measure-process-work.py'
    $geometryCpuToolHash='';$geometryBindingHash=''
    if ($CpuBinding) {
        $CpuBinding=(Resolve-Path -LiteralPath $CpuBinding).Path
        $geometryPins[$CpuBinding]=(Get-FileHash -LiteralPath $CpuBinding).Hash.ToLowerInvariant()
        $geometryPins[$geometryCpuTool]=(Get-FileHash -LiteralPath $geometryCpuTool).Hash.ToLowerInvariant()
        $geometryCpuToolHash=$geometryPins[$geometryCpuTool];$geometryBindingHash=$geometryPins[$CpuBinding]
        $geometryBindingData=Get-Content -LiteralPath $CpuBinding -Raw | ConvertFrom-Json
        if ($geometryBindingData.exe_sha256 -ne $geometryPins[$geometryExe] -or $geometryBindingData.map_sha256 -ne $geometryPins[$geometryMap]) { throw 'CPU binding differs from frozen EXE/map' }
    }
    $geometryOptions=@([regex]::Matches((Get-Content -LiteralPath $geometryRuntime -Raw),'"(BUZZ_GEOMETRY_JOB_[A-Z0-9_]+)"') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if (-not ($geometryOptions -contains 'BUZZ_GEOMETRY_JOB_INPUT_CONTROL')) { throw 'Missing production input options' }
    if ($Option -and ($Option -notin $geometryOptions -or $Option -like 'BUZZ_GEOMETRY_JOB_AUDIT*')) { throw 'Option must be a production job switch' }
    $geometryKnob=if ($Option) { $Option } else { 'JOB' }
    $geometryWindow=if ($Scene -eq 'Menu') { @(40,98) } else { @(200,270) }
    New-Item -ItemType Directory -Path $geometryOut | Out-Null
    $geometryProgress=Join-Path $geometryOut 'progress.jsonl'
    $geometryIndex=0
    foreach ($geometryArm in @(0,1,1,0)) {
        & ./tests/scripts/assert-geometry-build-idle.ps1
        foreach ($geometryPath in $geometryPins.Keys) {
            if ((Get-FileHash -LiteralPath $geometryPath).Hash.ToLowerInvariant() -ne $geometryPins[$geometryPath]) { throw "Measurement input changed: $geometryPath" }
        }
        $geometryIndex++
        $geometryJob=if ($Option) { 1 } else { $geometryArm }
        $geometryRunTag="$Tag-$($Scene.ToLowerInvariant())-$geometryIndex-$geometryArm"
        $geometrySave=Join-Path $geometryOut "save-$geometryRunTag"
        New-Item -ItemType Directory -Path $geometrySave | Out-Null
        Copy-Item -LiteralPath (Join-Path $geometryTemplate 'dev_hdd0') -Destination (Join-Path $geometrySave 'dev_hdd0') -Recurse
        if ((Get-FileHash -LiteralPath (Join-Path $geometrySave $geometrySaveRelative)).Hash.ToLowerInvariant() -ne $geometryPins[$geometryTemplateSave]) { throw 'Copied save differs from template' }
        $geometrySelected=[ordered]@{ BUZZ_GEOMETRY_JOB=$geometryJob }
        foreach ($geometryOption in $geometryOptions) { $geometrySelected[$geometryOption]=if ($geometryOption -like 'BUZZ_GEOMETRY_JOB_AUDIT*') { 0 } else { $geometryJob } }
        if ($Option) { $geometrySelected[$Option]=$geometryArm }
        $geometryEnvironment=(@($geometrySelected.Keys | ForEach-Object { "$_=$($geometrySelected[$_])" }) -join ';')
        $geometryEnvironment+=";BUZZ_GEOMETRY=1;BUZZ_GEOMETRY_KERNELS=827;BUZZ_STAGEF=1;BUZZ_IMAGE40_LOCALS=0;PS3_GAMEDATA_ROOT=$geometrySave;SPU_INTERP_STATS=0;SPU_INTERP_ENTRIES=0"
        $geometryRecord=[ordered]@{ tag=$geometryRunTag;scene=$Scene;metric=$Metric;job=$geometryJob;arm=$geometryArm;knob=$geometryKnob;index=$geometryIndex;window_seconds=$geometryWindow;exe_sha256=$geometryPins[$geometryExe];map_sha256=$geometryPins[$geometryMap];save_sha256=$geometryPins[$geometryTemplateSave];helper_sha256=$geometryPins[$geometryHelper];runner_sha256=$geometryPins[$geometryRunner];runtime_options_sha256=$geometryPins[$geometryRuntime];job_options=$geometrySelected;geometry_mask=827;stage_f=1;dump_every=0;cpu_binding_sha256=$geometryBindingHash;cpu_tool_sha256=$geometryCpuToolHash;round_intro_seconds=50;content_shot_at=195 }
        $geometryRecord.content_method=if ($Scene -eq 'Round') { 'gpu-once' } else { 'none' }
        $geometryRecord.content_capture_ms=if ($Scene -eq 'Round') { 190000 } else { 0 }
        $geometryRecord.content_tool_sha256=if ($Scene -eq 'Round') { $geometryPins[$geometryContentTool] } else { '' }
        [ordered]@{ state='start';run=$geometryRecord } | ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath $geometryProgress -Encoding utf8
        Write-Output "Starting $geometryRunTag ($Metric)"
        $geometryArgs=@{ Scene=$Scene;Tag=$geometryRunTag;Seconds=100;RoundSeconds=140;RoundIntroSeconds=50;BuildDirectory=$BuildDirectory;Env=$geometryEnvironment;PhaseLog=(Join-Path $geometryOut "$geometryRunTag.phases.jsonl") }
        if ($Scene -eq 'Round') { $geometryArgs.ContentReadbackAtMs=190000;$geometryArgs.ContentShotAt=195 }
        $geometryCpuPath=Join-Path $geometryOut "$geometryRunTag.cpu.json"
        if ($CpuBinding) { $geometryArgs.CpuBinding=$CpuBinding;$geometryArgs.CpuOutput=$geometryCpuPath;$geometryArgs.CpuWindow=$geometryWindow }
        & $geometryHelper @geometryArgs *> (Join-Path $geometryOut "$geometryRunTag.run.log")
        $geometryCsv=Join-Path $geometryRoot "analysis/vk-scene-$geometryRunTag.csv"
        $geometryLog=Join-Path $geometryRoot "analysis/vk-scene-$geometryRunTag.stderr.log"
        $geometryRows=Import-Csv -LiteralPath $geometryCsv
        if ($geometryRows.Count -lt 500 -or [double]$geometryRows[-1].elapsed_ms -lt 1000*$geometryWindow[1]) { throw 'Incomplete measurement window' }
        $geometryCpuHash=if ($CpuBinding) { (Get-FileHash -LiteralPath $geometryCpuPath).Hash.ToLowerInvariant() } else { '' }
        [ordered]@{ state='done';run=$geometryRecord;record_count=$geometryRows.Count;csv_sha256=(Get-FileHash -LiteralPath $geometryCsv).Hash.ToLowerInvariant();log_sha256=(Get-FileHash -LiteralPath $geometryLog).Hash.ToLowerInvariant();cpu_sha256=$geometryCpuHash } | ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath $geometryProgress -Encoding utf8
        Write-Output "Completed $geometryRunTag"
    }
    Write-Output "Complete reversed $Metric series: $geometryProgress"
} finally {
    foreach ($geometryKey in $geometryClearedPrevious.Keys) { [Environment]::SetEnvironmentVariable($geometryKey,$geometryClearedPrevious[$geometryKey],'Process') }
    Pop-Location
}
