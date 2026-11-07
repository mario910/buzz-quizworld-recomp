# Optimization status and reproducible profiles

This is the index of retained optimizations, optional experiments and diagnostics.
The generated tables below cover every port-owned CMake `option`, string tuning
switch, optional Android Gradle property and embedded runtime value. The pinned
SDK has additional internal settings; its full configuration is not an Android
preset. Build switches, environment overrides and in-game preferences are
separate layers. Setting an environment variable cannot restore code omitted
at compile time. Most environment options are cached: change them before launch.

## Which configuration is enabled?

| Configuration | Enabled behavior | What it does not imply |
|---|---|---|
| Bare CMake / Gradle | Source defaults in the generated tables; shared unconditional adapters below | Not the tested phone profile; most optional Android paths default to false |
| Supported Windows EU 1.00 native build | Hash-bound geometry/animation/completion adapters and embedded `eu100-vulkan-native-v1` defaults when CMake's qualification checks pass | Does not enable every `BUZZ_RENDERER_*` experiment; other game families have separate bindings |
| Linux x86-64 preparation build | Shared runtime/renderer improvements; exact options from `tools/build-linux.py` and its generated cache | Not the Android NEON profile; a reused cache can retain research options |
| Android phone reference 29 | Every choice explicitly listed in [reference29.properties](../android/profiles/reference29.properties); NEON host geometry, selected audio/video leaves, renderer and scheduler options | A comparison baseline, not independently proven wins for every enabled switch; Turnip is a separate runtime driver selection |
| Android deferred-clip experiment (30) | Reference29 plus `buzzGeometryDeferredClip=true` | Not promoted: repeated phone tests did not show a whole-game FPS benefit; default remains false |

The November 6 phone comparison used two runs per version, the same animated
menu and Turnip: reference29 19.52 FPS, deferred-clip 19.42 FPS; P95 78.14 versus
80.11 ms. The difference was smaller than the run-to-run spread. Stable 30 FPS
is not achieved by this profile. A faster isolated kernel does not establish
higher game FPS. Private traces/APKs remain outside Git.

## Shared retained changes (no separate opt-in switch)

These run when their enclosing backend/native path is available. Unsupported
inputs still take the exact reference fallback; diagnostic instrumentation can
also disable a fast path.

| Area | Retained implementation | Admission / fallback and validation |
|---|---|---|
| Renderer metadata | Bounded vertex-program input cache with full-byte verification; redundant fragment-constant clear removed | Mutated program bytes invalidate hits; synthetic renderer/program tests |
| Vertex planning and remapping | Active-attribute planning and SSE2/NEON remap primitives | `BUZZ_VERTEX_PLAN_ACTIVE_ONLY=0` and `BUZZ_VERTEX_REMAP_SIMD=0` disable the individual runtime routes; bounded input and original ordering tests |
| Geometry storage | Owned host SoA input/skin/clip/index/output pipeline; direct publication | Full host job must be admitted before external writes; whole-job lifted fallback; captured producer/output comparisons |
| Geometry transforms | Bounded ordinary-domain object-position path; matrix/input magnitude admission, exact RTZ/FZ and zero handling | Exceptional/unbounded operands use checked arithmetic; no approximate reciprocal substitution |
| Geometry culling | Reject an eight-triangle group before coordinate/area work when existing masks reject every active lane; skip disabled back-face work | Stable index order, tails and final-group count; mutation/rejection tests |
| Geometry codec tables | Compile-time expected index/cull tables | All guest table bytes still checked; no pointer-only cache |
| SPU dispatch | Bounded audited pure-loop fusion and a combined private history TLS object | Interrupts/observers/tracing disable fusion; `BUZZ_SPU_PURE_LOOPS_OFF=1` restores ordinary dispatch; history/step wrap tests |
| ARM integer helpers | Native NEON byte shuffle and immediate shifts/rotates | Exact instruction-level checks on ARM and x86; external-store/debug fallback retained |
| ARM floating point | Scoped FPCR/FPSR, checked ordinary arithmetic, cold exact exceptional repair; job-level FP scope | Does not enable unchecked fast-math; state/rounding tests and job corpus comparisons |
| Video leaves | Audited register-local converter/transform implementation | Only selected generated adapter; external LS/debug paths remain reference; `BUZZ_VIDEO_IDCT=0/2` disables/compares |
| Runtime compilation | Optimized PPU bridge/hot TU and SDK runtime; lifted SPU optimization separately controlled | `PORT_OPTIMIZE_PPU_HOT_PATHS` and `PORT_SPU_OPT`; large generated PPU units intentionally retain lower optimization to bound compile cost |
| Frame clock | Rational 60 Hz deadlines and bounded interruptible idle wait; actual presentation counter | 60 presentations can repeat movie frames; test unique decoded-frame progression separately |
| SDK job staging | Context reuse and declared-region clearing, sequence/wake/merge and cached options | Existing pinned SDK behavior; absent environment variables do not mean these are off |

Implementation references: [renderer](renderer.md), [native jobs](native-jobs.md),
[geometry layouts](native-geometry-data.md), [runtime defaults](runtime-defaults.md)
and [performance mechanisms and measurement rules](performance.md).

## Optional paths: status, dependencies and rollback

All optional renderer/scheduler experiments below are OFF in bare CMake unless
the generated default table says otherwise. The Android reference column below
is authoritative for which ones were actually packaged on the phone.

| Switch/family | Status and dependencies | Runtime rollback in an enabled build |
|---|---|---|
| `BUZZ_RENDERER_CPU_REFINEMENTS` | Reference29 ON: fragment hash/metadata refinements; content-validated caches | `BUZZ_FP_HASH_CACHE=0` disables structural-hash caching |
| `BUZZ_RENDERER_VERTEX_FETCH` | Reference29 ON: specialized conversion; independent of four-vertex streaming | `BUZZ_VERTEX_FETCH=0` |
| `BUZZ_RENDERER_TEXTURE_SNAPSHOT` | Reference29 ON: one snapshot of texture registers per decision | `BUZZ_TEXTURE_SNAPSHOT=0` |
| `BUZZ_RENDERER_DIRECT_DRAW`, `BUZZ_RENDERER_CONSTANT_RANGES` | Reference29 ON: remove redundant remap/index copies and clear only active constants | Rebuild OFF |
| `BUZZ_RENDERER_VERTEX_STREAM` | Reference29 ON: four-vertex SSE/NEON conversion, exact half/normal semantics | `BUZZ_VERTEX_STREAM=0` |
| `BUZZ_RENDERER_INDEX_FUSION` | Reference29 ON: fuse qualified u16 reference expansion/remap | `BUZZ_INDEX_FUSION=0` |
| `BUZZ_RENDERER_PACKED_FUSION` | Reference29 ON: private stamped table; requires index-fusion route; CPU saving is not a demonstrated FPS win | Rebuild OFF, or disable index fusion entirely |
| `BUZZ_VK_DESCRIPTOR_BATCH` | Reference29 ON: reuse retired immutable descriptor sets | `BUZZ_VK_DESCRIPTOR_BATCH=0` |
| `BUZZ_VK_UNIFORM_REUSE` | Reference29 ON: identical immutable constants within a batch | `BUZZ_VK_UNIFORM_REUSE=0`; `BUZZ_VK_UNIFORM_SETS=0` disables its set-level reuse subpath |
| `BUZZ_RSX_READY_CHECK` | Reference29 ON: avoid idle timeout when new FIFO work is already ready | `BUZZ_RSX_READY_CHECK=0` |
| `BUZZ_PPU_WORD_WAIT` | Reference29 ON: bounded wait/notification at reviewed busy-word polling sites, with normal retry fallback | Rebuild OFF; this does not permit skipping completion/fence semantics |
| `BUZZ_SPURS_POOL_AUTO_SPIN_US=50` | Reference29 tuning: changes automatic pool polling only on hosts with 12+ logical CPUs; tested 8-CPU phone still parks at 0 | `-1` retains SDK policy; explicit `SPURS_POOL_SPIN_US` overrides it |
| `BUZZ_SPURS_IDLE_SPIN_US` | Reference29 -1: no unconditional polling override; 0 parks, 1..1000 polls in microseconds | `SPURS_POOL_SPIN_US` and `SPURS_JTS_SPIN_US` override independently |
| `BUZZ_VK_BATCH_UPLOAD` | OFF: experimental queued immutable texture uploads, requires backend admission | `BUZZ_VK_BATCH_UPLOAD=0` |
| `BUZZ_VK_DESCRIPTOR_WRITES` | OFF: skip unchanged bindings only on retired descriptor sets | `BUZZ_VK_DESCRIPTOR_WRITES=0` |
| `BUZZ_VK_DRAW_STATE` | OFF: exact draw-state reuse within a pass; no established package-level FPS win | `BUZZ_VK_DRAW_STATE=0` |
| `BUZZ_RENDERER_STATE_PACKETS`, `BUZZ_RSX_METHOD_FASTPATH` | OFF: batched pure register packets / compact method dispatch; retain normal FIFO stops and side effects | `BUZZ_STATE_PACKETS=0`, `BUZZ_RSX_METHOD_FASTPATH=0` |
| `BUZZ_RENDERER_PIPELINE_LOOKUP` | OFF: full-key-validated pipeline lookup with linear fallback; no demonstrated FPS win | `BUZZ_PIPELINE_LOOKUP=0` |
| `BUZZ_RENDERER_NORMAL_MULTIPLY` | OFF: needs vertex stream and round-to-nearest finite domain; otherwise division | `BUZZ_NORMAL_MULTIPLY=0` |
| `BUZZ_RENDERER_INDEX_PREPARE` | OFF: bulk reference expansion and dense packed remap; independent experiment | `BUZZ_INDEX_APPEND=0`, `BUZZ_PACKED_REMAP=0` |
| `BUZZ_GEOMETRY_INDEX_SEQUENTIAL`, `BUZZ_GEOMETRY_INDEX_BATCHED` | OFF: producer-side flag validation / packed delta decoding; do not assume cumulative wins | Rebuild OFF |
| `BUZZ_GEOMETRY_CULL_COMPACT` | Windows default ON, other platforms OFF; Android reference OFF | Rebuild with the explicit platform choice |
| `BUZZ_GEOMETRY_DEFERRED_CLIP` | OFF; build30 rejected as an FPS upgrade after phone ABBA; exceptional blocks recomputed exactly | Rebuild OFF |
| `BUZZ_GEOMETRY_NATIVE_RECIPROCAL` | OFF: experimental native SIMD division replaces SPU reciprocal estimation/interpolation/refinement for ordinary finite W; low projection bits and quantized culling coordinates may differ. Extreme/zero/extended W retains the reference path. Captured retained triangles agree; paired PC menu testing does not establish stable 45 FPS or gameplay qualification. Android reference29 does not enable it | Rebuild OFF; takes precedence over deferred clip when both are enabled |
| `BUZZ_GEOMETRY_BOUNDED_SKIN` | OFF: experimental bounded palette blending and weighted transforms. Palette coefficients are checked once; ordinary inputs use native SIMD without per-operation SPU range checks, with exact fallback for other ranges. Captured output comparisons pass on x86 and ARM64/QEMU; combined PC CPU savings do not establish stable 45 FPS. Android reference29 does not enable it | Rebuild OFF |
| `BUZZ_GEOMETRY_COARSE_CLIP` | OFF: experimental interval bounds reject whole jobs outside one clip plane before projecting every vertex; bounded input/matrix/viewport, same-sign normal W and codec admission are required. Other jobs retain full projection/culling. Captured final-output comparisons pass on Windows, Linux x86 and ARM64/QEMU; paired PC menu testing does not establish a repeatable incremental FPS gain. Android reference29 does not enable it | Rebuild OFF |
| `BUZZ_GEOMETRY_LAZY_ATTRIBUTES` | OFF: rigid/unskinned jobs prepare positions before culling and decode/transform other attributes only when triangles survive. Weighted skinning stays in one pass. Full captured output/admission comparisons pass on Windows, Linux x86 and ARM64/QEMU; paired PC tests do not establish a meaningful heavy-scene FPS gain. Android property `buzzGeometryLazyAttributes` defaults false and is OFF in reference29 | Rebuild OFF |
| `BUZZ_SPURS_POOL_LAYOUT` | OFF: separates polling/cache-line writes; no task-order change and no established FPS win | Rebuild OFF |
| `BUZZ_RSX_PARK_WAKE` | OFF: notify exact watched self-JUMP replacement; needs ordinary SPURS DMA wake path | `BUZZ_RSX_PARK_WAKE=0` |
| `BUZZ_WINDOWS_TIMER_REUSE` | OFF, Windows-only timer reuse experiment | Rebuild OFF |

Runtime switches apply only where their enclosing path is compiled. Exact
behavior and further admission checks live in the linked CMake/header sources;
[performance.md](performance.md) explains these mechanisms in detail.

## Native modes and diagnostics

- `BUZZ_GEOMETRY_JOB` selects the complete native job; `0` restores lifted
  execution. Per-stage `BUZZ_GEOMETRY_JOB_*` controls are legacy diagnostic
  routes, not a checklist to enable the host pipeline. See [native jobs](native-jobs.md).
- Image7/36, retained geometry and Stage F modes generally use `0` for the
  reference, `1` native and `2` compare/reference-result mode. Availability is
  decided by generated source/ISA options, not the mode variable alone.
- Image39's kernel hook is currently a fallback stub. Its checked FP option
  changes arithmetic helpers, not the retained producer's instruction flow.
  Native whole-job animation is Image40; see [native jobs](native-jobs.md).
- Android reference29 explicitly enables Image7, Image36, Image39/40 checked FP,
  full Image41 host geometry and video IDCT generation. The native PC animation
  profile must not be assumed enabled on Android merely because both have
  `BUZZ_GEOMETRY_JOB=1`.
- Android standalone adds `SPURS_NATIVE_IMAGE39_FP=1` only with its image39 NEON
  adapter; the ARM geometry adapter adds Stage F kernels 6. The native outer
  driver adds `BUZZ_GEOMETRY_JOB=1` and layout/publish/vertex routes. Native
  completion mode 2 is added for the disc standalone profile. These are
  conditional additions from `port/runtime_defaults.cpp`, not bare Gradle defaults.
- `PORT_IMAGE7_COMPARE`, `PORT_SPU_LS_DEBUG_HOOKS`, `BUZZ_RENDERER_FETCH_PROFILE`,
  `BUZZ_RENDERER_WAIT_TRACE`, `BUZZ_PRODUCER_DEPENDENCY_TRACE`,
  `BUZZ_PPU_WORD_WAIT_TRACE`, `BUZZ_SPURS_EXECUTION_TRACE` and generated
  `PORT_VIDEO_SOURCE` / `PORT_PRODUCER_PROBE_DIR` are diagnostics. Keep them out
  of timing builds. Standalone SPU LS probes default OFF (ON in non-standalone
  developer builds); Android explicitly sets OFF.
- Job corpus capture/replay, geometry traces/snapshots, dump/compare modes,
  `BUZZ_RENDER_PERF`, `BUZZ_TRACE_TRIGGER`, `SPURS_JOB_TRACE_ALL`, `SPU_PCHIST`
  and descriptor/pipeline profiling are opt-in diagnostics, not optimizations.
  Android's `files/runtime/profile-frames` marker enables frame CSV and Vulkan
  counters only after restart; remove it and restart to disable.
- `BUZZ_NO_FMV`, disabled audio/effects, lowered resolution and an in-game FPS
  cap change the workload or presentation policy. They are not equivalent
  performance comparisons. Driver choice is separate from code optimization.

## Reproducing and validating a profile

Use [the Android build guide](../android/README.md) for a fresh checkout and the
explicit reference property file. `CMakeCache.txt`, generated-source identities,
APK/EXE hashes and selected driver determine what was tested. `--runtime-defaults`
prints installed environment defaults on desktop; it does not enumerate every
compile-time optimization. A new source tree or faster PC kernel does not
reproduce an old executable automatically.

Run `python tools/document-optimizations.py --check` after changing build flags,
Gradle properties or embedded defaults. Refresh with the same command without
`--check`; update the hand-written status/dependency sections when behavior changes.

<!-- generated-options:start -->

## Build switches (source defaults)

Defaults below are declarations, not an existing CMake cache. Values in `${...}`
are conditional; the source link contains the admission/build condition.
A cached `ON` survives reconfiguration unless explicitly reset.

| Switch | Source default | Meaning / defining source |
|---|---|---|
| `BUZZ_EXPERIMENTAL_VULKAN` | `OFF` | Developer-only Vulkan game build; keep outside build-windows ([source](../port/CMakeLists.txt)) |
| `PORT_ENABLE_SPU` | `ON` | Compile and register the title's lifted SPU images ([source](../port/CMakeLists.txt)) |
| `PORT_IMAGE7_COMPARE` | `OFF` | Compare Image 7 against its exact oracle on Android ([source](../port/CMakeLists.txt)) |
| `PORT_SPU_LS_DEBUG_HOOKS` | `${spu_probe_default}` | Keep SPU_LS_WATCH/SPU_SMC_WATCH probes in lifted SPU images ([source](../port/CMakeLists.txt)) |
| `PORT_NATIVE_SPU_FLOAT` | `OFF` | Native RZ/FMA for the audited single-precision geometry image only ([source](../port/CMakeLists.txt)) |
| `PORT_RTZ_RESULT_GUARD` | `OFF` | Experimental result classification instead of operand exponent checks; same exact lane repair ([source](../port/CMakeLists.txt)) |
| `BUZZ_STANDALONE` | `OFF` | Build for the prepared distribution layout ([source](../port/CMakeLists.txt)) |
| `BUZZ_DISC_COMPLETION` | `OFF` | Enable the validated original disc completion mode ([source](../port/CMakeLists.txt)) |
| `PORT_GEOMETRY_HOST_NEON` | `OFF` | Use the native host SoA geometry pipeline on ARM64 ([source](../port/CMakeLists.txt)) |
| `PORT_OPTIMIZE_PPU_HOT_PATHS` | `ON` | Optimize PPU bridge and profiled job-poll TU ([source](../port/CMakeLists.txt)) |
| `PORT_NATIVE_JOB_DEFAULTS` | `${native_job_default_candidate}` | Embed the approved native/Vulkan runtime profile in the qualified EXE ([source](../port/CMakeLists.txt)) |
| `BUZZ_GEOMETRY_INDEX_SEQUENTIAL` | `OFF` | Bulk-validated sequential geometry index decoding ([source](../port/CMakeLists.txt)) |
| `BUZZ_GEOMETRY_DEFERRED_CLIP` | `OFF` | Defer exceptional geometry projection repair to one block check ([source](../port/CMakeLists.txt)) |
| `BUZZ_GEOMETRY_NATIVE_RECIPROCAL` | `OFF` | Use native SIMD reciprocal division for ordinary geometry projection (relaxed intermediate values) ([source](../port/CMakeLists.txt)) |
| `BUZZ_GEOMETRY_BOUNDED_SKIN` | `OFF` | Use native bounded palette blending and weighted transforms ([source](../port/CMakeLists.txt)) |
| `BUZZ_GEOMETRY_COARSE_CLIP` | `OFF` | Reject bounded whole geometry jobs before per-vertex projection ([source](../port/CMakeLists.txt)) |
| `BUZZ_GEOMETRY_LAZY_ATTRIBUTES` | `OFF` | Defer rigid geometry attributes until triangles survive culling ([source](../port/CMakeLists.txt)) |
| `BUZZ_GEOMETRY_INDEX_BATCHED` | `OFF` | Eight-lane packed geometry delta decoding ([source](../port/CMakeLists.txt)) |
| `BUZZ_GEOMETRY_CULL_COMPACT` | `${WIN32}` | Eight-lane geometry output compaction ([source](../port/CMakeLists.txt)) |
| `PORT_SPU_ISA` | `sse2` | Lifted x86 SPU helpers: sse2, sse41, avx2 or experimental avx512 ([source](../port/CMakeLists.txt)) |
| `PORT_SPU_INPUTS` | `empty` | Input files to scan for all embedded SPU ELF programs (semicolon-separated decrypted ELF/PRX/data paths) ([source](../port/CMakeLists.txt)) |
| `PORT_SPU_OPT` | `-O2` | Optimization level of the lifted SPU TUs ([source](../port/CMakeLists.txt)) |
| `BUZZ_RSX_METHOD_FASTPATH` | `OFF` | Experimental compact FIFO method dispatch loop ([source](../cmake/BuzzFifoMethods.cmake)) |
| `BUZZ_RSX_PARK_WAKE` | `OFF` | Wake RSX when DMA replaces its observed parked jump ([source](../cmake/BuzzFifoParkWake.cmake)) |
| `BUZZ_RSX_READY_CHECK` | `OFF` | Avoid sleeping when a completed FIFO drain has new work ([source](../cmake/BuzzFifoReady.cmake)) |
| `BUZZ_PPU_WORD_WAIT` | `OFF` | Experimental portable DMA notification for registered PPU busy-word waits ([source](../cmake/BuzzPpuWordWait.cmake)) |
| `BUZZ_PPU_WORD_WAIT_TRACE` | `OFF` | Diagnostic PPU wait and exact-word DMA timeline ([source](../cmake/BuzzPpuWordWait.cmake)) |
| `BUZZ_RENDERER_CONSTANT_RANGES` | `OFF` | Clear only previously active shader constants ([source](../cmake/BuzzRendererConstantRanges.cmake)) |
| `BUZZ_RENDERER_CPU_REFINEMENTS` | `OFF` | Experimental shader metadata and vertex preparation refinements ([source](../cmake/BuzzRendererCpu.cmake)) |
| `BUZZ_RENDERER_FETCH_PROFILE` | `OFF` | Diagnostic vertex-format counters ([source](../cmake/BuzzRendererCpu.cmake)) |
| `BUZZ_RENDERER_VERTEX_FETCH` | `OFF` | Specialized vertex conversion ([source](../cmake/BuzzRendererCpu.cmake)) |
| `BUZZ_RENDERER_DIRECT_DRAW` | `OFF` | Eliminate redundant array remap and triangle index copy ([source](../cmake/BuzzRendererDirectDraw.cmake)) |
| `BUZZ_RENDERER_INDEX_FUSION` | `OFF` | Fuse contiguous u16 index expansion and remapping ([source](../cmake/BuzzRendererIndex.cmake)) |
| `BUZZ_RENDERER_PACKED_FUSION` | `OFF` | Use compact private stamps for fused u16 remapping ([source](../cmake/BuzzRendererIndex.cmake)) |
| `BUZZ_RENDERER_INDEX_PREPARE` | `OFF` | Bulk index references and packed dense remapping ([source](../cmake/BuzzRendererIndexPrepare.cmake)) |
| `BUZZ_RENDERER_PIPELINE_LOOKUP` | `OFF` | Index the existing RSX pipeline key table ([source](../cmake/BuzzRendererPipelineLookup.cmake)) |
| `BUZZ_RENDERER_STATE_PACKETS` | `OFF` | Batch pure RSX register state packets ([source](../cmake/BuzzRendererStatePackets.cmake)) |
| `BUZZ_RENDERER_TEXTURE_SNAPSHOT` | `OFF` | Decode texture registers once per pipeline decision ([source](../cmake/BuzzRendererTextures.cmake)) |
| `BUZZ_RENDERER_VERTEX_STREAM` | `OFF` | Convert common vertex layouts four vertices at a time ([source](../cmake/BuzzRendererVertexStream.cmake)) |
| `BUZZ_RENDERER_NORMAL_MULTIPLY` | `OFF` | Exact finite-domain CMP normalization with multiplication ([source](../cmake/BuzzRendererVertexStream.cmake)) |
| `BUZZ_RENDERER_WAIT_TRACE` | `OFF` | Detailed RSX/FIFO/Vulkan wait timeline ([source](../cmake/BuzzRendererWait.cmake)) |
| `BUZZ_PRODUCER_DEPENDENCY_TRACE` | `OFF` | Trace DMA publications and FIFO park addresses (diagnostics only) ([source](../cmake/BuzzRendererWait.cmake)) |
| `BUZZ_SPURS_EXECUTION_TRACE` | `OFF` | Diagnostic SPURS execution CPU counters and native-thread channel waits ([source](../cmake/BuzzSpursExecutionTrace.cmake)) |
| `BUZZ_SPURS_POOL_LAYOUT` | `OFF` | Separate SPURS polling and bookkeeping cache lines ([source](../cmake/BuzzSpursPoolLayout.cmake)) |
| `BUZZ_SPURS_POOL_AUTO_SPIN_US` | `-1` | Automatic SPURS worker poll on 12+ logical CPUs (-1: SDK policy, 0..1000 us) ([source](../cmake/BuzzSpursPoolSpin.cmake)) |
| `BUZZ_SPURS_IDLE_SPIN_US` | `-1` | Experimental SPURS idle polling in microseconds (-1: SDK policy, 0..1000: override) ([source](../cmake/BuzzSpursWaits.cmake)) |
| `BUZZ_VK_BATCH_UPLOAD` | `OFF` | Queue immutable texture uploads with draw batches ([source](../cmake/BuzzVulkanUpload.cmake)) |
| `BUZZ_VK_DESCRIPTOR_BATCH` | `OFF` | Reuse retired immutable draw descriptor sets ([source](../cmake/BuzzVulkanUpload.cmake)) |
| `BUZZ_VK_DESCRIPTOR_WRITES` | `OFF` | Skip unchanged bindings of retired draw descriptor sets ([source](../cmake/BuzzVulkanUpload.cmake)) |
| `BUZZ_VK_UNIFORM_REUSE` | `OFF` | Reuse identical immutable constants within a draw batch ([source](../cmake/BuzzVulkanUpload.cmake)) |
| `BUZZ_VK_DRAW_STATE` | `OFF` | Reuse exact draw state within one render pass ([source](../cmake/BuzzVulkanUpload.cmake)) |
| `BUZZ_WINDOWS_TIMER_REUSE` | `OFF` | Experimental reusable high-resolution syscall timer ([source](../cmake/BuzzWindowsTimer.cmake)) |

## Android properties

The reference column is the explicit phone comparison profile, not a claim
that every enabled experiment independently increases FPS. A bare Gradle build
uses the default column. Paths (`buzzWorkspace`, `buzzPython`, `sdlSource`) are required separately.

| Gradle `-P` property | Bare default | Reference 29 |
|---|---|---|
| `buzzNeonImage36` | `false` | `true` |
| `buzzNeonImage7` | `false` | `true` |
| `buzzCompareImage7` | `false` | `false` |
| `buzzNeonJobFp` | `false` | `true` |
| `buzzNeonGeometry` | `false` | `false` |
| `buzzNeonGeometryDriver` | `false` | `true` |
| `buzzNeonHostGeometry` | `false` | `true` |
| `buzzNeonVideoIdct` | `false` | `true` |
| `buzzRendererCpuRefinements` | `false` | `true` |
| `buzzBatchedTextureUploads` | `false` | `false` |
| `buzzSpecializedVertexFetch` | `false` | `true` |
| `buzzTextureSnapshot` | `false` | `true` |
| `buzzDescriptorBatch` | `false` | `true` |
| `buzzDescriptorWrites` | `false` | `false` |
| `buzzDrawState` | `false` | `false` |
| `buzzStatePackets` | `false` | `false` |
| `buzzDirectDraw` | `false` | `true` |
| `buzzConstantRanges` | `false` | `true` |
| `buzzVertexStream` | `false` | `true` |
| `buzzRsxReadyCheck` | `false` | `true` |
| `buzzRsxParkWake` | `false` | `false` |
| `buzzRsxMethodFastpath` | `false` | `false` |
| `buzzPpuWordWait` | `false` | `true` |
| `buzzUniformReuse` | `false` | `true` |
| `buzzIndexFusion` | `false` | `true` |
| `buzzPackedIndexFusion` | `false` | `true` |
| `buzzGeometryDeferredClip` | `false` | `false` |
| `buzzGeometryLazyAttributes` | `false` | `false` |
| `buzzIndexPrepare` | `false` | `false` |
| `buzzGeometryIndexSequential` | `false` | `false` |
| `buzzGeometryIndexBatched` | `false` | `false` |
| `buzzSpursPoolLayout` | `false` | `false` |
| `buzzGeometryCullCompact` | `false` | `false` |
| `buzzSpursIdleSpinUs` | `-1` | `-1` |
| `buzzSpursPoolAutoSpinUs` | `-1` | `50` |

## Embedded runtime values

From `port/runtime_defaults.inc`. Common values install in standalone builds;
the native-only section requires the qualified native profile. Explicit environment
values win, including `0`. Android adds its conditional values described above.

| Variable | Embedded value | Scope |
|---|---|---|
| `PS3_VERBOSE` | `0` | Common standalone / qualified native |
| `CULL_FLIP` | `1` | Common standalone / qualified native |
| `GCM_SECOND_V` | `1` | Common standalone / qualified native |
| `SPURS_JOB_ABI_MIN_IMAGE` | `32` | Common standalone / qualified native |
| `SPURS_JOB_ABI_STAGE_IMAGE` | `42` | Common standalone / qualified native |
| `SPURS_JOB_KERNEL_MIRROR` | `1` | Common standalone / qualified native |
| `GCM_STRICT_PARK` | `1` | Common standalone / qualified native |
| `GCM_FIFO_STOP_AT_FLIP` | `1` | Common standalone / qualified native |
| `RSX_FLIP_BOUNDARY` | `1` | Common standalone / qualified native |
| `RSX_VERTEX_CAPACITY` | `1048576` | Common standalone / qualified native |
| `RSX_DRAW_CAPACITY` | `8192` | Common standalone / qualified native |
| `RSX_TEXTURE_CAPACITY` | `256` | Common standalone / qualified native |
| `SPURS_JOB_LIST_WORKERS` | `4` | Common standalone / qualified native |
| `SPURS_JOB_LIST_IMAGE` | `41` | Common standalone / qualified native |
| `SPURS_JOB_LIST_EXTRA_IMAGES` | `40` | Common standalone / qualified native |
| `SPURS_JOB_LS_EXCHANGE` | `1` | Common standalone / qualified native |
| `SPURS_JOB_LS_PUBLISH` | `mbox` | Common standalone / qualified native |
| `RSX_PIPELINE_CAPACITY` | `1024` | Common standalone / qualified native |
| `BUZZ_COMPLETION_QUEUE` | `1` | Common standalone / qualified native |
| `RSX_TRACK_ZETA` | `1` | Common standalone / qualified native |
| `BUZZ_STAGEF` | `1` | Common standalone / qualified native |
| `SPURS_POOL_SEQUENCE` | `1` | Common standalone / qualified native |
| `SPU_INTERP_UNLIFTED` | `1` | Common standalone / qualified native |
| `PS3_USLEEP_TIMER_TAIL_US` | `150` | Common standalone / qualified native |
| `SPURS_JOB_CACHE_ENV` | `1` | Common standalone / qualified native |
| `RSX_CACHE_ENV` | `1` | Common standalone / qualified native |
| `SPURS_GUARD_POLL_ONLY` | `0` | Common standalone / qualified native |
| `SPURS_JTS_WAKE` | `1` | Common standalone / qualified native |
| `SPURS_JTS_DMA_WAKE` | `1` | Common standalone / qualified native |
| `SPURS_MERGE_READY_LISTS` | `1` | Common standalone / qualified native |
| `SPURS_NATIVE_EXTRA_FP` | `1` | Common standalone / qualified native |
| `RSX_GUEST_SAMPLERS` | `1` | Common standalone / qualified native |
| `RSX_PIXEL_WINDOW` | `1` | Common standalone / qualified native |
| `PS3RECOMP_VK_DRAW_BATCH` | `512` | Common standalone / qualified native |
| `PS3RECOMP_VK_DRAW_RING` | `1` | Common standalone / qualified native |
| `PS3RECOMP_VK_DRAW_PASSES` | `1` | Common standalone / qualified native |
| `PS3RECOMP_VK_DRAW_CLEARS` | `1` | Common standalone / qualified native |
| `PS3RECOMP_VK_DRAW_ASYNC` | `1` | Common standalone / qualified native |
| `PS3RECOMP_VK_PRESENT_MODE` | `mailbox` | Common standalone / qualified native |
| `PS3RECOMP_VK_PRESENT_READBACK` | `0` | Common standalone / qualified native |
| `PS3RECOMP_VK_TEXTURE_REUSE` | `1` | Common standalone / qualified native |
| `PS3RECOMP_ENG_TEXTURE_ROWS` | `1` | Common standalone / qualified native |
| `PS3RECOMP_ENG_VERTEX_BULK` | `1` | Common standalone / qualified native |
| `PS3RECOMP_ENG_PROGRAM_PAIR_ONCE` | `1` | Common standalone / qualified native |
| `BUZZ_NATIVE_COMPLETION` | `2` | Qualified native only |
| `BUZZ_IMAGE7` | `1` | Qualified native only |
| `BUZZ_IMAGE36` | `1` | Qualified native only |
| `BUZZ_GEOMETRY` | `1` | Qualified native only |
| `BUZZ_GEOMETRY_JOB` | `1` | Qualified native only |
| `BUZZ_ANIMATION_JOB` | `1` | Qualified native only |

<!-- generated-options:end -->
