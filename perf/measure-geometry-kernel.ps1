# Same-executable A/B then B/A in menu and round; each run starts with the same
# private save copy. Freeze the validated EXE first and do not compile/profile
# concurrently with these measurement windows.
param([Parameter(Mandatory)][ValidatePattern('^build-[A-Za-z0-9_-]+$')][string]$BuildDirectory,
      [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag,
      [Parameter(Mandatory)][ValidateRange(1,8191)][int]$KernelMask,
      [ValidateRange(0,8191)][int]$BaselineMask = 0,
      [Parameter(Mandatory)][string]$SaveTemplate,
      [ValidateRange(100,600)][int]$MenuSeconds = 100,
      [ValidateRange(95,600)][int]$RoundSeconds = 95)
$ErrorActionPreference = 'Stop'
if ($KernelMask -band $BaselineMask) { throw 'KernelMask must contain new bits, separate from BaselineMask.' }
$geometryRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $geometryRoot
try {
    $geometryOut = Join-Path $geometryRoot "analysis/native-geometry/phase2/$Tag"
    if (Test-Path -LiteralPath $geometryOut) { throw 'Use a fresh tag; measurement output already exists.' }
    $geometryTemplate = (Resolve-Path -LiteralPath $SaveTemplate).Path
    if (-not (Test-Path -LiteralPath "$geometryTemplate/dev_hdd0/home")) { throw 'Save template has no private dev_hdd0/home' }
    New-Item -ItemType Directory -Path $geometryOut | Out-Null
    $geometryExe = Join-Path $geometryRoot "$BuildDirectory/BuzzQuizWorldRecomp.exe"
    $geometryHash = (Get-FileHash -LiteralPath $geometryExe -Algorithm SHA256).Hash
    $geometryProgress = Join-Path $geometryOut 'progress.jsonl'
    foreach ($geometryScene in @('Menu','Round')) {
        $geometryIndex = 0
        foreach ($geometryEnabled in @(0,1,1,0)) {
            $geometryIndex++
            if ((Get-FileHash -LiteralPath $geometryExe -Algorithm SHA256).Hash -ne $geometryHash) { throw 'Measurement EXE changed' }
            $geometryRunTag = "$Tag-$($geometryScene.ToLowerInvariant())-$geometryIndex-$geometryEnabled"
            $geometrySave = Join-Path $geometryOut "save-$geometryRunTag"
            New-Item -ItemType Directory -Path $geometrySave | Out-Null
            Copy-Item -LiteralPath "$geometryTemplate/dev_hdd0" -Destination "$geometrySave/dev_hdd0" -Recurse
            $geometrySelected = if ($geometryEnabled) { $BaselineMask -bor $KernelMask } else { $BaselineMask }
            $geometryEnv = "BUZZ_GEOMETRY=1;BUZZ_GEOMETRY_KERNELS=$geometrySelected;BUZZ_STAGEF=1;PS3_GAMEDATA_ROOT=$geometrySave"
            if ($geometryScene -eq 'Round') {
                # One occasional readback in the round, identically configured
                # for both modes, supplies native renderer evidence.
                $geometryEnv += ";PS3RECOMP_VK_DUMP=$geometryOut/$geometryRunTag.ppm;PS3RECOMP_VK_DUMP_EVERY=6500"
            }
            @{ state='start'; tag=$geometryRunTag; scene=$geometryScene; mask=$geometrySelected; exe_sha256=$geometryHash; time=(Get-Date).ToString('o') } |
                ConvertTo-Json -Compress | Add-Content -LiteralPath $geometryProgress -Encoding utf8
            Write-Output "Starting $geometryRunTag"
            $geometryRunLog = Join-Path $geometryOut "$geometryRunTag.run.log"
            & (Join-Path $PSScriptRoot 'measure-vulkan-scene.ps1') -Scene $geometryScene -Tag $geometryRunTag -Seconds $MenuSeconds -RoundSeconds $RoundSeconds -Env $geometryEnv -BuildDirectory $BuildDirectory *> $geometryRunLog
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
            @{ state='done'; tag=$geometryRunTag; scene=$geometryScene; mask=$geometrySelected; frames=$geometryRows.Count; exe_sha256=$geometryHash; time=(Get-Date).ToString('o') } |
                ConvertTo-Json -Compress | Add-Content -LiteralPath $geometryProgress -Encoding utf8
            Write-Output "Completed $geometryRunTag ($($geometryRows.Count) frames)"
        }
    }
    "A/B and B/A complete: $geometryProgress"
} finally { Pop-Location }
