# Same-executable A/B then B/A in menu and round; each run starts with the same
# private save copy. Freeze the validated EXE first and do not compile/profile
# concurrently with these measurement windows.
# Up to three isolated kernels can share the two baseline runs. For A/B/C,
# order is baseline,A,B,C,C,B,A,baseline, giving every kernel its own reversal.
param([Parameter(Mandatory)][ValidatePattern('^build-[A-Za-z0-9_-]+$')][string]$BuildDirectory,
      [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag,
      [Parameter(Mandatory)][ValidateRange(1,8191)][int[]]$KernelMask,
      [ValidateRange(0,8191)][int]$BaselineMask = 0,
      [Parameter(Mandatory)][string]$SaveTemplate,
      [switch]$MenuDumps,
      [switch]$NoDumps,
      [ValidateSet('Menu','Round')][string[]]$Scenes = @('Menu','Round'),
      [string]$CpuBinding = '',
      [ValidateRange(100,600)][int]$MenuSeconds = 100,
      [ValidateRange(95,600)][int]$RoundSeconds = 95)
$ErrorActionPreference = 'Stop'
if ($NoDumps -and $MenuDumps) { throw 'NoDumps and MenuDumps are mutually exclusive' }
if ($CpuBinding -and -not $NoDumps) { throw 'Use NoDumps for a separate CPU comparison' }
if (-not $Scenes.Count -or ($Scenes | Select-Object -Unique).Count -ne $Scenes.Count) { throw 'Select distinct scenes' }
if (-not $KernelMask.Count -or $KernelMask.Count -gt 3 -or ($KernelMask | Select-Object -Unique).Count -ne $KernelMask.Count) {
    throw 'Supply one to three distinct KernelMask values.'
}
foreach ($geometryKernel in $KernelMask) {
    if ($geometryKernel -band $BaselineMask) { throw 'KernelMask must contain new bits, separate from BaselineMask.' }
    if ($KernelMask.Count -gt 1 -and ($geometryKernel -band ($geometryKernel - 1))) {
        throw 'A shared-baseline batch requires one independent bit per kernel.'
    }
}
$geometryRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $geometryRoot
try {
    $geometryOut = Join-Path $geometryRoot "analysis/native-geometry/phase2/$Tag"
    if (Test-Path -LiteralPath $geometryOut) { throw 'Use a fresh tag; measurement output already exists.' }
    $geometryTemplate = (Resolve-Path -LiteralPath $SaveTemplate).Path
    if (-not (Test-Path -LiteralPath "$geometryTemplate/dev_hdd0/home")) { throw 'Save template has no private dev_hdd0/home' }
    $geometrySaveRelative = 'dev_hdd0/home/00000001/savedata/BCES00647/SAVEDATA'
    $geometryTemplateSave = Join-Path $geometryTemplate $geometrySaveRelative
    if (-not (Test-Path -LiteralPath $geometryTemplateSave -PathType Leaf)) { throw 'Save template needs the reviewed EU saved profiles.' }
    $geometrySaveHash = (Get-FileHash -LiteralPath $geometryTemplateSave -Algorithm SHA256).Hash
    New-Item -ItemType Directory -Path $geometryOut | Out-Null
    $geometryExe = Join-Path $geometryRoot "$BuildDirectory/BuzzQuizWorldRecomp.exe"
    $geometryHash = (Get-FileHash -LiteralPath $geometryExe -Algorithm SHA256).Hash
    $geometryCpuHash = ''
    $geometryCpuToolHash = ''
    if ($CpuBinding) {
        $CpuBinding = (Resolve-Path -LiteralPath $CpuBinding).Path
        $geometryCpuHash = (Get-FileHash -LiteralPath $CpuBinding -Algorithm SHA256).Hash
        $geometryCpuToolHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'measure-process-work.py') -Algorithm SHA256).Hash
        $geometryBindingData = Get-Content -LiteralPath $CpuBinding -Raw | ConvertFrom-Json
        if ($geometryBindingData.exe_sha256 -ne $geometryHash) { throw 'CPU binding differs from measured EXE' }
    }
    $geometryProgress = Join-Path $geometryOut 'progress.jsonl'
    foreach ($geometryScene in $Scenes) {
        $geometryIndex = 0
        $geometryArmOrder = @(0) + @(1..($KernelMask.Count)) + @(($KernelMask.Count)..1) + @(0)
        foreach ($geometryArm in $geometryArmOrder) {
            $geometryIndex++
            if ((Get-FileHash -LiteralPath $geometryExe -Algorithm SHA256).Hash -ne $geometryHash) { throw 'Measurement EXE changed' }
            if ((Get-FileHash -LiteralPath $geometryTemplateSave -Algorithm SHA256).Hash -ne $geometrySaveHash) { throw 'Measurement template save changed' }
            if ($CpuBinding -and (Get-FileHash -LiteralPath $CpuBinding -Algorithm SHA256).Hash -ne $geometryCpuHash) { throw 'CPU binding changed' }
            if ($CpuBinding -and (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'measure-process-work.py') -Algorithm SHA256).Hash -ne $geometryCpuToolHash) { throw 'CPU observer changed' }
            $geometryRunTag = "$Tag-$($geometryScene.ToLowerInvariant())-$geometryIndex-$geometryArm"
            $geometrySave = Join-Path $geometryOut "save-$geometryRunTag"
            New-Item -ItemType Directory -Path $geometrySave | Out-Null
            Copy-Item -LiteralPath "$geometryTemplate/dev_hdd0" -Destination "$geometrySave/dev_hdd0" -Recurse
            if ((Get-FileHash -LiteralPath (Join-Path $geometrySave $geometrySaveRelative) -Algorithm SHA256).Hash -ne $geometrySaveHash) {
                throw 'Copied save differs from the measurement template'
            }
            $geometryAddition = if ($geometryArm) { $KernelMask[$geometryArm - 1] } else { 0 }
            $geometrySelected = $BaselineMask -bor $geometryAddition
            $geometryEnv = "BUZZ_GEOMETRY=1;BUZZ_GEOMETRY_KERNELS=$geometrySelected;BUZZ_STAGEF=1;PS3_GAMEDATA_ROOT=$geometrySave"
            $geometryDumpEvery = if ($geometryScene -eq 'Round') { 6500 } elseif ($MenuDumps) { 2500 } else { 0 }
            if ($NoDumps) { $geometryDumpEvery = 0 }
            if ($geometryDumpEvery) {
                # Same occasional GPU readback in every arm, including baseline.
                $geometryEnv += ";PS3RECOMP_VK_DUMP=$geometryOut/$geometryRunTag.ppm;PS3RECOMP_VK_DUMP_EVERY=$geometryDumpEvery"
            }
            @{ state='start'; tag=$geometryRunTag; scene=$geometryScene; mask=$geometrySelected; arm=$geometryArm; added_kernel_mask=$geometryAddition; dump_every=$geometryDumpEvery; save_sha256=$geometrySaveHash; exe_sha256=$geometryHash; cpu_binding_sha256=$geometryCpuHash; cpu_tool_sha256=$geometryCpuToolHash; time=(Get-Date).ToString('o') } |
                ConvertTo-Json -Compress | Add-Content -LiteralPath $geometryProgress -Encoding utf8
            Write-Output "Starting $geometryRunTag"
            $geometryRunLog = Join-Path $geometryOut "$geometryRunTag.run.log"
            $geometryCpuArgs = @{}
            $geometryCpuPath = Join-Path $geometryOut "$geometryRunTag.cpu.json"
            if ($CpuBinding) { $geometryCpuArgs = @{ CpuBinding=$CpuBinding; CpuOutput=$geometryCpuPath } }
            & (Join-Path $PSScriptRoot 'measure-vulkan-scene.ps1') -Scene $geometryScene -Tag $geometryRunTag -Seconds $MenuSeconds -RoundSeconds $RoundSeconds -Env $geometryEnv -BuildDirectory $BuildDirectory @geometryCpuArgs *> $geometryRunLog
            $geometryCpuResultHash = ''
            if ($CpuBinding) { $geometryCpuResultHash = (Get-FileHash -LiteralPath $geometryCpuPath -Algorithm SHA256).Hash.ToLowerInvariant() }
            $geometryCsv = Join-Path $geometryRoot "analysis/vk-scene-$geometryRunTag.csv"
            $geometryRows = Import-Csv -LiteralPath $geometryCsv
            $geometryExpected = $MenuSeconds
            if ($geometryScene -eq 'Round') {
                $geometryMatch = [regex]::Match((Get-Content -LiteralPath $geometryRunLog -Raw), 'round_start_s=([0-9.,]+)')
                if (-not $geometryMatch.Success) { throw 'Missing verified navigation milestone' }
                $geometryExpected = [double]::Parse($geometryMatch.Groups[1].Value.Replace(',','.'), [Globalization.CultureInfo]::InvariantCulture) + $RoundSeconds
            }
            if ($geometryRows.Count -lt 500 -or [double]$geometryRows[-1].elapsed_ms -lt ($geometryExpected - 5)*1000) {
                throw "Incomplete measurement $geometryRunTag"
            }
            @{ state='done'; tag=$geometryRunTag; scene=$geometryScene; mask=$geometrySelected; arm=$geometryArm; added_kernel_mask=$geometryAddition; dump_every=$geometryDumpEvery; save_sha256=$geometrySaveHash; frames=$geometryRows.Count; exe_sha256=$geometryHash; cpu_binding_sha256=$geometryCpuHash; cpu_tool_sha256=$geometryCpuToolHash; cpu_sha256=$geometryCpuResultHash; time=(Get-Date).ToString('o') } |
                ConvertTo-Json -Compress | Add-Content -LiteralPath $geometryProgress -Encoding utf8
            Write-Output "Completed $geometryRunTag ($($geometryRows.Count) frames)"
        }
    }
    "A/B and B/A complete: $geometryProgress"
} finally { Pop-Location }
