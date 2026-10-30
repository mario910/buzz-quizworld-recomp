param([int]$Seconds = 40, [switch]$NativeSpurs, [switch]$MenuBootstrap, [switch]$OriginalVideo, [switch]$SkipVideo, [switch]$Mute, [switch]$Background, [string]$InputConfig = '', [string]$PathsConfig = '', [string]$BuildDirectory = '')
$projectRoot = (Split-Path -Parent $PSScriptRoot)

$ErrorActionPreference = 'Stop'
$experimentRoot = $projectRoot
# -BuildDirectory build-windows runs the D3D12 fallback build.
# Vulkan is the default renderer when its build exists; D3D12 otherwise.
if (-not $BuildDirectory) {
    $BuildDirectory = if (Test-Path -LiteralPath (Join-Path $experimentRoot 'build-vulkan-windows\BuzzQuizWorldRecomp.exe')) { 'build-vulkan-windows' } else { 'build-windows' }
}
$exe = Join-Path (Join-Path $experimentRoot $BuildDirectory) 'BuzzQuizWorldRecomp.exe'
$existingGame = @(Get-Process -Name BuzzQuizWorldRecomp -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $exe })
if ($existingGame.Count) {
    throw "Experimental game is already running (PID: $($existingGame.Id -join ', ')). Close that instance before starting another one."
}
. (Join-Path $experimentRoot 'scripts/buzz-paths.ps1')
$resolvedInput = ''
if ($PSBoundParameters.ContainsKey('InputConfig')) {
    if (-not $InputConfig) { throw 'InputConfig must name an existing configuration file' }
    $resolvedInput = (Resolve-Path -LiteralPath $InputConfig -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $resolvedInput -PathType Leaf)) { throw 'InputConfig must be a file' }
}
$gamePaths = Get-BuzzPaths -ConfigPath $PathsConfig
$boundBuild=Join-Path (Join-Path $experimentRoot $BuildDirectory) 'game-build.json'
if(Test-Path -LiteralPath $boundBuild) {
    $binding=Get-Content -LiteralPath $boundBuild -Raw | ConvertFrom-Json
    if((Get-BuzzFileSha256 -Path $gamePaths.eboot_elf) -ne $binding.elf_sha256) {
        throw 'This EXE was generated for another game executable. Use the launcher to select a matching prepared build.'
    }
    if((Get-BuzzFileSha256 -Path $exe) -ne $binding.exe_sha256) {
        throw 'The EXE no longer matches its build manifest. Rebuild or verify the prepared session before launching.'
    }
}
$runtime = Join-Path $experimentRoot 'runtime'
$logDirectory = Join-Path $experimentRoot 'logs'
$spuDump = Join-Path $experimentRoot 'analysis\raw-jobs\runtime'
New-Item -ItemType Directory -Force -Path $logDirectory,$runtime,(Join-Path $runtime 'dev_hdd0'),(Join-Path $runtime 'dev_hdd1'),$spuDump | Out-Null
$env:PS3_TITLE = 'Buzz! Quiz World'
$env:PS3_SPURS_CAPTURE = '1'
$env:SPU_DUMP_MISS = $spuDump
if ([string]::IsNullOrEmpty($env:PS3_VERBOSE)) { $env:PS3_VERBOSE = '0' }
# RSX and D3D12 disagree on the front-face orientation after viewport Y
# inversion. Keep culling enabled, but use the verified winding convention.
$env:CULL_FLIP = '1'
# Keyboard belongs to the USB buzzers, not simultaneously to a DualShock.
$env:PAD_NO_KEYBOARD = '1'
# The full ABI exposes additional SPU->jobchain scheduling that is not yet
# complete. Keep that investigation separate from the verified input/UI boot.
if ($NativeSpurs) {
    $env:SPURS_JOB_ABI_STACK = '1'
    $env:SPURS_JOB_KERNEL_MIRROR = '1'
} else {
    Remove-Item Env:SPURS_JOB_ABI_STACK -ErrorAction SilentlyContinue
    Remove-Item Env:SPURS_JOB_KERNEL_MIRROR -ErrorAction SilentlyContinue
}
if ($MenuBootstrap) {
    $bootstrapEnvironment = @{}
    $bootstrapEnvironment['BUZZ_NATIVE_COMPLETION'] = [Environment]::GetEnvironmentVariable('BUZZ_NATIVE_COMPLETION','Process')
    if($binding -and $binding.runtime_defaults) {
        foreach($option in $binding.runtime_defaults.PSObject.Properties) {
            if($option.Name -notin @('BUZZ_NATIVE_COMPLETION')) {throw 'Unknown build-bound runtime option'}
            if([string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($option.Name,'Process'))) {
                [Environment]::SetEnvironmentVariable($option.Name,[string]$option.Value,'Process')
            }
        }
    }
    foreach ($key in @('BUZZ_NO_FMV','GCM_SECOND_V','SPURS_JOB_ABI_MIN_IMAGE','SPURS_JOB_ABI_STAGE_IMAGE','SPURS_JOB_KERNEL_MIRROR','GCM_STRICT_PARK','RSX_VERTEX_CAPACITY','RSX_DRAW_CAPACITY','RSX_TEXTURE_CAPACITY','BUZZ_PULSE_MS','SPURS_JOB_LIST_WORKERS','SPURS_JOB_LIST_IMAGE','SPURS_JOB_LIST_EXTRA_IMAGES','SPURS_JOB_LS_EXCHANGE','SPURS_JOB_LS_PUBLISH','BUZZ_COMPLETION_QUEUE','BUZZ_STAGEF','SPURS_POOL_SEQUENCE','SPURS_POOL_SPIN_US','SPU_INTERP_UNLIFTED','PS3_USLEEP_TIMER_TAIL_US','RSX_TRACK_ZETA','RSX_SHADER_CACHE_DIR','RSX_PIPELINE_CAPACITY','SPURS_JOB_CACHE_ENV','RSX_CACHE_ENV','SPURS_GUARD_POLL_ONLY','SPURS_JTS_WAKE','SPURS_JTS_DMA_WAKE','GCM_FIFO_STOP_AT_FLIP','RSX_FLIP_BOUNDARY','SPURS_MERGE_READY_LISTS','SPURS_NATIVE_EXTRA_FP','RSX_GUEST_SAMPLERS','RSX_PIXEL_WINDOW')) {
        $bootstrapEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    }
    # Startup films (Relentless logo, intro) play: they decode and hand over
    # to the menu since the 2026-10-28 runtime. -SkipVideo answers the Lua
    # video queries as errored instead (benchmarks: fixed menu timeline).
    # -OriginalVideo is the default now and kept for older command lines.
    # The newly reached scene jobs run with their real ABI stack.
    $env:BUZZ_NO_FMV = if($SkipVideo){'1'}else{'0'}
    # Native menu movies also need the real video tick. Their conversion queue
    # is implemented now; skipping startup Lua movies must not suppress them.
    # An explicit 0 remains available to isolate that subsystem in diagnostics.
    if ([string]::IsNullOrEmpty($env:GCM_SECOND_V)) { $env:GCM_SECOND_V = '1' }
    # Every Buzz job image (32..44) gets the ABI's available-stack word (R1
    # word 1). With 0 their GCC stack checks halted: the load-screen clapperboard
    # (image 37, LS 0xFA8) and menu image 40 (0xF30) jobs aborted every frame.
    if ([string]::IsNullOrEmpty($env:SPURS_JOB_ABI_MIN_IMAGE)) { $env:SPURS_JOB_ABI_MIN_IMAGE = '32' }
    $env:SPURS_JOB_ABI_STAGE_IMAGE = '42'
    $env:SPURS_JOB_KERNEL_MIRROR = '1'
    $env:GCM_STRICT_PARK = '1'
    # Finish at an actual committed FIFO flip. Scene complexity is not a
    # completeness test: the old clear/max-draw heuristic merged small frames.
    # SDK defaults stay compatible with other titles; this experiment opts in.
    if ([string]::IsNullOrEmpty($env:GCM_FIFO_STOP_AT_FLIP)) { $env:GCM_FIFO_STOP_AT_FLIP = '1' }
    if ([string]::IsNullOrEmpty($env:RSX_FLIP_BOUNDARY)) { $env:RSX_FLIP_BOUNDARY = '1' }
    # Captured studio frames request ~783k vertices and ~4944 draws. The SDK
    # defaults silently clipped the latter half of the frame, including UI.
    $env:RSX_VERTEX_CAPACITY = '1048576'
    $env:RSX_DRAW_CAPACITY = '8192'
    # The studio exhausts the SDK's 96 cached textures before its UI draws.
    $env:RSX_TEXTURE_CAPACITY = '256'
    # Independent geometry only. Native service jobs remain outside the pool;
    # the SDK itself keeps its serial/live-settings defaults.
    if ([string]::IsNullOrEmpty($env:SPURS_JOB_LIST_WORKERS)) { $env:SPURS_JOB_LIST_WORKERS = '4' }
    if ([string]::IsNullOrEmpty($env:SPURS_JOB_LIST_IMAGE)) { $env:SPURS_JOB_LIST_IMAGE = '41' }
    # Image 40 lists ran serially at the start of every frame. Each pooled job
    # keeps an exclusive virtual-SPU number (menu at equal geometry load:
    # 43.2 -> 40.0 ms/frame, same HALT-ASSERT count, image unchanged).
    if ($null -eq $env:SPURS_JOB_LIST_EXTRA_IMAGES) { $env:SPURS_JOB_LIST_EXTRA_IMAGES = '40' }
    # Hand the finished LS to the snapshot instead of copying 256 KiB under the
    # chain's shared lock after every pooled job (phase-aligned menu window:
    # 24.8 -> 28.7 FPS together with pooled image 40).
    if ([string]::IsNullOrEmpty($env:SPURS_JOB_LS_EXCHANGE)) { $env:SPURS_JOB_LS_EXCHANGE = '1' }
    # Hand the LS to the printf snapshot only after jobs that posted a mailbox;
    # the rest keep a cache-warm context (image41 setup 3.0 -> 1.9 us/job).
    if ($null -eq $env:SPURS_JOB_LS_PUBLISH) { $env:SPURS_JOB_LS_PUBLISH = 'mbox' }
    # New scenes compiled dozens of translated shaders synchronously on the RSX
    # thread (a ~0.6 s frame entering a round). Keep their bytecode on disk
    # (exact source match on load) and stop the 256-entry FIFO PSO cache from
    # evicting pipelines still in use.
    if ($null -eq $env:RSX_SHADER_CACHE_DIR) { $env:RSX_SHADER_CACHE_DIR = Join-Path $runtime 'shader-cache' }
    if ([string]::IsNullOrEmpty($env:RSX_PIPELINE_CAPACITY)) { $env:RSX_PIPELINE_CAPACITY = '1024' }
    # The slot producers sleep until a slot completes instead of spinning on
    # TryWait: same FPS on 16 threads, -6.5% CPU, +2.5% FPS on 4C/8T.
    if ([string]::IsNullOrEmpty($env:BUZZ_COMPLETION_QUEUE)) { $env:BUZZ_COMPLETION_QUEUE = '1' }
    # Per-address Z24S8 surfaces, sampled as textures by the depth-of-field
    # pass. Without them it read an all-zero buffer and blurred the whole 3D
    # scene; menu and round are sharp with it, at no measured cost.
    if ([string]::IsNullOrEmpty($env:RSX_TRACK_ZETA)) { $env:RSX_TRACK_ZETA = '1' }
    # Native 8-wide stage F of the EDGE geometry job (vertex transform and
    # triangle cull), bit-exact against the lift: corpus of 2831 jobs and
    # 6.65M in-game compared calls. Needs a -StageF build; ignored otherwise.
    if ([string]::IsNullOrEmpty($env:BUZZ_STAGEF)) { $env:BUZZ_STAGEF = '1' }
    # Worker-side continuation between pool groups (docs/perf-notes.md); =0 disables.
    if ([string]::IsNullOrEmpty($env:SPURS_POOL_SEQUENCE)) { $env:SPURS_POOL_SEQUENCE = '1' }
    # Idle pool lanes poll ~1 ms for the next SYNC-separated stage instead of an
    # OS wake per stage (menu 27.4 -> 30.0-31.6 FPS). SDK: 12+ logical CPUs only.
    if ([string]::IsNullOrEmpty($env:SPURS_POOL_SPIN_US)) { $env:SPURS_POOL_SPIN_US = '1000' }
    # Sound: MultiStream (SPU audio mixer) calls ATRAC/DSP modules it loads into
    # local store at runtime; no lift can exist for them, so interpret them.
    # Without this the mixer task ends on its first call and the game is silent.
    if ([string]::IsNullOrEmpty($env:SPU_INTERP_UNLIFTED)) { $env:SPU_INTERP_UNLIFTED = '1' }
    # Sub-ms guest sleeps block on a high-resolution timer and spin only the
    # last 150 us; the MultiStream update thread polls with ~800 us sleeps.
    if ([string]::IsNullOrEmpty($env:PS3_USLEEP_TIMER_TAIL_US)) { $env:PS3_USLEEP_TIMER_TAIL_US = '150' }
    if ([string]::IsNullOrEmpty($env:SPURS_JOB_CACHE_ENV)) { $env:SPURS_JOB_CACHE_ENV = '1' }
    if ([string]::IsNullOrEmpty($env:RSX_CACHE_ENV)) { $env:RSX_CACHE_ENV = '1' }
    # Real producer notifications now cover both command publication and the
    # guard owner's native PC replacement. Both retain a 1-ms safety poll.
    if ([string]::IsNullOrEmpty($env:SPURS_GUARD_POLL_ONLY)) { $env:SPURS_GUARD_POLL_ONLY = '0' }
    if ([string]::IsNullOrEmpty($env:SPURS_JTS_WAKE)) { $env:SPURS_JTS_WAKE = '1' }
    # Geometry jobs publish the other chain's next JOBLIST by SPU DMA PUT; wake
    # the parked walker on that PUT instead of its 1-ms bound (menu A/B
    # 15.3-16.0 -> 21.7-22.1 FPS, image41 work unchanged). 0 restores the poll.
    if ([string]::IsNullOrEmpty($env:SPURS_JTS_DMA_WAKE)) { $env:SPURS_JTS_DMA_WAKE = '1' }
    # Retain a tap across low-rate guest input updates. Two-second presses
    # could survive a screen transition; queued key events now preserve taps.
    if ([string]::IsNullOrEmpty($env:BUZZ_PULSE_MS) -and -not ($resolvedInput -or $env:BUZZ_INPUT_CONFIG)) { $env:BUZZ_PULSE_MS = '250' }
    # Carry the verified title settings into ordinary launches, not just ad-hoc
    # benchmark shells. Explicit '0' still selects the reference path.
    if ([string]::IsNullOrEmpty($env:SPURS_MERGE_READY_LISTS)) { $env:SPURS_MERGE_READY_LISTS = '1' }
    if ([string]::IsNullOrEmpty($env:SPURS_NATIVE_EXTRA_FP)) { $env:SPURS_NATIVE_EXTRA_FP = '1' }
    if ([string]::IsNullOrEmpty($env:RSX_GUEST_SAMPLERS)) { $env:RSX_GUEST_SAMPLERS = '1' }
    if ([string]::IsNullOrEmpty($env:RSX_PIXEL_WINDOW)) { $env:RSX_PIXEL_WINDOW = '1' }
}
# The Vulkan build's speedups are all opt-in (analysis/vulkan-performance-handoff.md);
# without them it runs a few FPS. Codex's measured-best set, unless already set.
if ($BuildDirectory -like '*vulkan*') {
    $vulkanDefaults = [ordered]@{ PS3RECOMP_VK_DRAW_BATCH = '512'; PS3RECOMP_VK_DRAW_RING = '1';
        PS3RECOMP_VK_DRAW_PASSES = '1'; PS3RECOMP_VK_DRAW_CLEARS = '1'; PS3RECOMP_VK_DRAW_ASYNC = '1';
        PS3RECOMP_VK_PRESENT_MODE = 'mailbox'; PS3RECOMP_VK_PRESENT_READBACK = '0';
        PS3RECOMP_VK_TEXTURE_REUSE = '1'; PS3RECOMP_ENG_TEXTURE_ROWS = '1' }
    foreach ($key in $vulkanDefaults.Keys) {
        if ([string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($key))) {
            [Environment]::SetEnvironmentVariable($key, $vulkanDefaults[$key], 'Process')
        }
    }
}
$env:PS3_VFS_ROOT = $gamePaths.game_root
$env:PS3_DISC_ROOT = $env:PS3_VFS_ROOT
$env:PS3_HDD0_ROOT = Join-Path $runtime 'dev_hdd0'
$env:PS3_HDD1_ROOT = Join-Path $runtime 'dev_hdd1'
$env:PS3_DEV_FLASH = $gamePaths.dev_flash
$elf = $gamePaths.eboot_elf
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stdout = Join-Path $logDirectory "$stamp.stdout.log"
$stderr = Join-Path $logDirectory "$stamp.stderr.log"
$inputConfigBefore = [Environment]::GetEnvironmentVariable('BUZZ_INPUT_CONFIG', 'Process')
$muteBefore = [Environment]::GetEnvironmentVariable('PS3_MUTE', 'Process')
try {
    if ($resolvedInput) { $env:BUZZ_INPUT_CONFIG = $resolvedInput }
    if ($MenuBootstrap) {
        Write-Output "Native config: workers=$env:SPURS_JOB_LIST_WORKERS image=$env:SPURS_JOB_LIST_IMAGE+$env:SPURS_JOB_LIST_EXTRA_IMAGES job-cache=$env:SPURS_JOB_CACHE_ENV rsx-cache=$env:RSX_CACHE_ENV merge=$env:SPURS_MERGE_READY_LISTS extra-fp=$env:SPURS_NATIVE_EXTRA_FP samplers=$env:RSX_GUEST_SAMPLERS pixel-window=$env:RSX_PIXEL_WINDOW jts-dma-wake=$env:SPURS_JTS_DMA_WAKE ls-exchange=$env:SPURS_JOB_LS_EXCHANGE ls-publish=$env:SPURS_JOB_LS_PUBLISH color-grid=$env:RSX_PACK_AA_COLOR no-fmv=$env:BUZZ_NO_FMV second-v=$env:GCM_SECOND_V"
    }
    # -Background: the game window opens without focus, beneath other windows.
    # -Mute: silence at the audio device only. The game, its mixer and the
    # audio thread run exactly as with sound (CPU/timing unchanged): for tests.
    if ($Mute) { $env:PS3_MUTE = '1' }
    if ($Background) { $env:RSX_WINDOW_BACKGROUND = '1' } else { Remove-Item Env:RSX_WINDOW_BACKGROUND -ErrorAction SilentlyContinue }
    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $elf + '"') -WorkingDirectory $runtime -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
} finally {
    if ($Mute) {
        if ($null -eq $muteBefore) { Remove-Item Env:PS3_MUTE -ErrorAction SilentlyContinue }
        else { [Environment]::SetEnvironmentVariable('PS3_MUTE', $muteBefore, 'Process') }
    }
    if ($PSBoundParameters.ContainsKey('InputConfig')) {
        if ($null -eq $inputConfigBefore) { Remove-Item Env:BUZZ_INPUT_CONFIG -ErrorAction SilentlyContinue }
        else { [Environment]::SetEnvironmentVariable('BUZZ_INPUT_CONFIG', $inputConfigBefore, 'Process') }
    }
    if ($MenuBootstrap) {
        foreach ($key in $bootstrapEnvironment.Keys) {
            # Recent PowerShell/.NET preserve an empty environment entry.
            # A null saved value must REMOVE it, not leave getenv("")/atoi("")
            # disabling workers/caches on the next launch in the same shell.
            if ($null -eq $bootstrapEnvironment[$key]) {
                Remove-Item "Env:$key" -ErrorAction SilentlyContinue
            } else {
                [Environment]::SetEnvironmentVariable($key, $bootstrapEnvironment[$key], 'Process')
            }
        }
    }
}
Write-Output "PID=$($process.Id) stdout=$stdout stderr=$stderr"
if ($Seconds -gt 0) {
    if (-not $process.WaitForExit($Seconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        Write-Output "Stopped game process after $Seconds seconds."
    } else {
        Write-Output "ExitCode=$($process.ExitCode)"
    }
    Get-Content -LiteralPath $stdout -Tail 30
    Get-Content -LiteralPath $stderr -Tail 65
}
