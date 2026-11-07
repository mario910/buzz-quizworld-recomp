# Profiling and native-job validation

Start with the [complete optimization catalog](optimization-status.md) for
source defaults, the explicit Android reference profile, retained shared paths
and experiments that remain disabled. This page explains their mechanisms and
measurement rules; it is not a preset that enables all of them.

Profile the current executable before choosing another optimization. Keep
performance observations tied to the exact build, scene and environment.
Absolute FPS from different execution environments is not a shared baseline;
the observed Codex/Claude difference has no established causal explanation.
Do not infer an aggregate speedup by adding gains from overlapping changes.

Keep a named PC baseline throughout an optimization series. Re-run its frozen
EXE alongside candidates in the same environment; report its identity, scene,
window, wait policy and renderer settings with every progress claim. A test
that changes the PC to a phone-like wait policy is a separate experiment,
not a replacement baseline. State both the fixed-PC result and the experimental
control when they differ. Historical higher FPS remains an unresolved result
until the old binary is re-tested; do not explain it as environment noise
without evidence. CPU work/frame, menu FPS and physical-phone FPS are separate
measurements and must not be presented interchangeably.

Before comparing runs, use `perf/compare-renderer-profiles.py` on their stderr
logs. It rejects mismatched actual swapchain extents, GPU, present mode, batch
size, frame cap, render scale and pool wait policy, and requires two draw
contexts. Requested window/guest dimensions alone are insufficient: DPI can
make the actual Vulkan swapchain differ. Name intentional changes explicitly
with `--vary pool_spin_us` for a wait-policy experiment. This check does not
validate scene equivalence, instrumentation or all inherited environment
variables; retain the full run configuration and inspect captures too.

## Job acceptance

For a native change require:

1. State the job-boundary contract. Preserve DMA bounds, publication order,
   atomic/channel effects and completion controls. Exact paths compare all
   published bytes (`BUZZ_REPLAY_CONTRACT=1`). Explicitly relaxed arithmetic may
   differ numerically when its declared error limits, rendering and game
   behavior are validated; intermediate register/LS equality is not required.
2. At least 10,000 completed whole-job contract checks, with live I/O coverage
   as appropriate to the declared contract. Report skipped comparisons and
   fallbacks separately; offline kernel equality alone is not game validation.
3. Inspected native menu and actual round frames. A settings screen is not
   round coverage; a black capture or a different scene invalidates the run.
4. Reversed same-EXE off/on/on/off menu and round FPS comparisons, plus a
   separate series for worker/process cycles and CPU time per completed frame.
5. Default-on only after correctness/render gates and repeatable performance
   benefit or parity. Flat FPS can still accompany useful worker CPU savings.

Internal register/LS diagnostics are optional; the declared job boundary and
rendered/game behavior define correctness. Unsupported inputs must select the
whole lift before mutation.

## Reproducible runs

Freeze builds as `build-vulkan-*`; manifests must include `renderer` and the
EXE hash. Keep the matching linker map and source binding. Preserve existing
frozen builds, private evidence and original saves; give each run a fresh
private save copy. Keep rendering, audio, resolution, frame cap, input timing
and capture settings fixed between arms.

Use the latest qualified build as the reference for further optimization,
with its ordinary platform policy. Historical builds and simulated phone
wait policies are separate experiments, not replacement baselines. Report
all repeated controls and candidates, the scene and device, and the measured
window. Keep CPU/cycle savings distinct from FPS gains. A depressed control
alone does not establish an improvement over the previously qualified result.

Run `python perf/compare-renderer-profiles.py <stderr-before> <stderr-after>`
to compare effective GPU, swapchain size, presentation mode, render scale,
frame cap, batch size, contexts and pool polling. Use `--vary` only for an
intentional policy experiment. This checks settings; executable hashes and
visual scene inspection are still required.

Every run log must contain both `window present mode mailbox` and
`two-context asynchronous draw batches enabled` for this Vulkan profile.
Never run another game, compilation, CPU test or independent profiler during
a measurement. Debug checkers and instruction counters are separate runs.

`perf/measure-vulkan-scene.ps1` drives matched menu/round scenes.
`perf/measure-geometry-job.ps1` compares whole-job modes;
`perf/measure-geometry-kernel.ps1` isolates retained leaf groups.
The associated `*-report.py` and `scene-window-stats.py` tools validate and
summarize their artifacts. Use each runner's configured renderer-relative
window; do not mix old round windows with later navigation schedules.
The EU 1.00 solo challenge entry uses eight downward presses in the Play menu;
`-SoloMenuSteps` permits an explicit layout override. The runner's legacy
`round_start_s` field marks completion of the ready-input sequence, not verified
gameplay. It emits `round_progression_verified=0` until question, answer and
score progression have been inspected independently. An animated player avatar
alone does not establish round coverage, including on a locked desktop.

Captures introduce work. Place a one-shot GPU readback before the timed
window, verify the PPM/PNG pixels, and inspect it after the run. Disable
continuous readback and profiling in FPS runs. A rejected or incomplete arm
requires a fresh complete matched series, not selective replacement of an
unfavorable result.

## CPU and function profiling

`perf/measure-process-work.py` binds an explicit EXE/map/PID and reads OS
process/thread CPU time and cycle counters plus renderer epoch/presentation
counters. It does not suspend threads or write guest memory. It reports the
whole process and the four named SPURS pool lanes per completed frame.
Use `--include-rsx` for the renderer and `--all-threads` to also retain other
owned thread handles before the interval. The latter ranks persistent PPU,
SPURS chain, driver and other workers and reports the difference from process
totals. Short-lived threads can be missing: discovery never runs inside the
measurement window. These are concurrent CPU costs, not additive wall-frame
times; polling CPU must not be labeled geometry work. Keep the exact timing
window with the result; a different scene interval is not an FPS regression.

Missing/recreated threads, changed identities, counter resets, late attachment
and unreliable frame denominators invalidate the measurement. Preserve raw
snapshots and endpoint uncertainty. CPU milliseconds and cycle counts are
independent metrics; do not convert cycles to time using nominal GHz.

`perf/geometry-rip-report.py` attributes sampled worker instruction pointers
to image/functions. Use image-specific attribution and a matching map to rank
current hotspots. Function call counts alone do not measure worker cost.

## Optional interpreter diagnostics

`SPU_INTERP_EXACT=1` arms cumulative atomic instruction-dispatch counters by
image and 1 KiB LS page. It counts actual executor dispatches, including
STOP/HALT, rather than entries or elapsed time. Disabled/unarmed counters
cannot prove zero activity. `perf/observe-spu-interpreter-exact.py` is the
read-only observer; keep it out of FPS/CPU series. The older sampled/entry
diagnostics are not interchangeable with exact instruction counts.

There is no outstanding round interpreter-count gate or cold-format coverage
campaign. Use these tools only when a current profile justifies them.

## Renderer wait attribution

To distinguish FIFO sleep, draw fences and presentation waits, build with
`-DBUZZ_RENDERER_WAIT_TRACE=ON` and set e.g.
`PS3_FTRACE=45:3:<absolute-output.trace>`. This optional adapter uses the SDK
trace clock/window and does not change the pinned SDK. Normal builds omit
these hooks. `python perf/renderer-waits.py <output.trace>` reports complete
executed-flip intervals, host presentation, idle waits, fence types, swapchain
acquisition and queue presentation. It rejects captures with dropped spans.
`PS3_FTRACE_CAP` can raise the record limit for busy scenes.

`python perf/producer-waits.py <output.trace> --output producer.json` intersects
RSX idle intervals with in-flight SPURS jobs, chain publication/guard waits,
pool queue-to-claim and completion-to-return spans. It unions parallel spans
before calculating overlap, clips to complete executed-flip intervals and
rejects dropped records. Categories overlap and must not be added. An in-flight
job can itself be waiting: these timings are not CPU utilization or proof of
a causal critical path. Use a separate trace-disabled FPS series to decide
whether shortening a producer phase improves end-to-end throughput.

For address-specific publication evidence, also enable
`-DBUZZ_PRODUCER_DEPENDENCY_TRACE=ON` and `SPURS_JOB_TRACE_ALL=1`.
`python perf/publication-dependencies.py <output.trace> --output dependencies.json`
matches observed small DMA writes to SPURS command slots and to self-JUMPs
subsequently consumed by RSX. It reports the publishing job image, the observed
word, consumer latency and unioned idle overlap. A diagnostic atomic filter
limits recording to previously observed wait locations (hash collisions only
add records). Only 4/8-byte DMA publications are recorded; larger writes, PPU
redirects and unmatched waits remain unknown. Values are post-copy snapshots,
not an atomic log of concurrent guest writers. Use matching consumed words and
separate CPU counters/FPS measurements before drawing throughput conclusions.

`BUZZ_RSX_PARK_WAKE` is an experimental CMake option, OFF by default. It uses
the existing SPURS DMA notification hook to signal the RSX event only when a
write replaces the exact self-JUMP currently watched by the consumer. It needs
the normal `SPURS_JTS_WAKE`/`SPURS_JTS_DMA_WAKE` path; other producers still use
the bounded retry. `BUZZ_RSX_PARK_WAKE=0` disables the watch at runtime. It has
not demonstrated a repeatable FPS benefit; keep it out of normal test packages
until a complete matched comparison establishes one.

On Windows, `perf/observe-presentation.py` can observe an exact EXE/map-bound
process without suspending it. It records actual window presentation, guest
present calls, FIFO flips and this process's client-area/minimized state.
Minimizing the game intentionally suppresses swapchain presentation while the
guest continues. A flat presentation counter alone is therefore not evidence
of a queue deadlock. Reject FPS windows containing minimization or a missing
presentation tail; never silently discard the affected comparison arm.
The observer's `validate_window` helper checks sample coverage, missing windows,
minimization and changed client size. If collecting it during FPS measurements,
use it in every comparison arm; do not mix observed and unobserved runs.
`--session-state` also records the target's console/RDP protocol, connection
state, lock flag and last connection/disconnection times. Select the session
policy before the series and require the same signature across every arm.
`session_window_signature(..., policy='unlocked')` retains the normal active,
unlocked requirement; `policy='locked'` supports a consistently locked console
or RDP session, including a disconnected session if rendering keeps progressing.
Never mix lock states, connection states/protocols or connection timestamps
within or across paired windows. A locked desktop is a supported benchmark
environment, not an automatic failure or a reason to require user interaction.
Record its actual swapchain extent, retain frame/progress/tail checks and inspect
captures; results apply to that recorded environment. For unattended PC work,
keep the desktop locked throughout the comparison and use the input mailbox
and renderer readbacks for game validation. A visible game window alone does
not establish comparable desktop conditions. `--cpu-load` adds aggregate
system and game CPU counters without enumerating unrelated processes. It can
show coincident CPU-load changes, not prove their cause or measure GPU load.

`BUZZ_RENDERER_PIPELINE_LOOKUP` and `BUZZ_RENDERER_NORMAL_MULTIPLY` are optional
experiments, OFF by default and excluded from qualified packages. The first
indexes existing pipeline keys with full-key validation and a linear fallback.
The second requires vertex-stream conversion and uses exact finite-domain CMP
normalization only in round-to-nearest, retaining division in other rounding
modes. Neither has established an FPS benefit. Runtime overrides in enabled
builds are `BUZZ_PIPELINE_LOOKUP=0` and `BUZZ_NORMAL_MULTIPLY=0` respectively.

`BUZZ_RENDERER_INDEX_PREPARE` enables two index-preparation paths: bounded,
restart-free u16/u32 reference expansion with SSE2/NEON, and a per-consumer
packed generation/index table for dense ranges up to 4096 entries. Both retain
the SDK fallback and first-use order; they do not cache guest data between
draws. In an enabled build, `BUZZ_INDEX_APPEND=0` and `BUZZ_PACKED_REMAP=0`
disable each path independently. Android exposes `-PbuzzIndexPrepare=true`;
the separate exact-park wake experiment uses `-PbuzzRsxParkWake=true`.
These build switches default to OFF. Qualify combinations with a complete
matched FPS series because less RSX work can become additional producer wait.

Producer-side experiments also default to OFF:
`BUZZ_GEOMETRY_INDEX_SEQUENTIAL` bulk-validates the consumed flags of index
blocks without explicit deltas. `BUZZ_GEOMETRY_INDEX_BATCHED` prepares bounded
delta blocks with AVX2/NEON and 4 KiB of worker-local scratch, retaining the
streaming decoder outside its admitted range. `BUZZ_SPURS_POOL_LAYOUT`
separates polling hints, queue bookkeeping and locks using 64-byte alignment;
it does not change scheduling or lane ownership. Android properties are
`buzzGeometryIndexSequential`, `buzzGeometryIndexBatched` and `buzzSpursPoolLayout`.
Measure these separately before combining them: kernel timing, reduced cache
sharing or lower CPU usage alone does not establish a presentation FPS gain.

`BUZZ_GEOMETRY_CULL_COMPACT` explicitly selects the eight-lane cull-output
compaction present in the qualified Windows executable. Its CMake default is
ON for Windows and OFF elsewhere; Android exposes `buzzGeometryCullCompact`
(default false). This makes the binary's existing choice reproducible from
source rather than relying on a previously compiled object.

Before building in a reused prepared workspace, use
`perf/sync-native-sources.py --source <checkout> --workspace <prepared> --manifest <json>`.
It writes changed native inputs with fresh timestamps and records their hashes.
Do not restore headers with timestamp-preserving copies: a header copied from
an archive can be older than the object containing the experiment being removed.
Source hashes alone cannot establish that a binary was rebuilt. Retain the
successful build log and bind the EXE/map hashes; when reconstructing a known
baseline, compare the binary as well. Generated guest sources and the SDK are
outside this synchronization helper and keep their separate preparation checks.

Fence durations are CPU-observed waits and may be nested inside FIFO drain or
presentation: never add nested times to their parents. GET/PUT snapshots
around idle waits can reveal producer activity, but inequality alone does
not prove runnable work because the FIFO can be parked on a jump. Use
separate runs with tracing disabled for FPS qualification.

`BUZZ_PPU_WORD_WAIT` (default OFF, Android `-PbuzzPpuWordWait=true`) replaces
the reviewed PPU busy-word polling syscall with a bounded portable wait.
The existing profile-specific registration identifies the caller and word;
only zero publications from the installed SPURS DMA notification route wake
it. The guest still rechecks its predicate. Missing notification support,
invalid input and full waiter capacity retain the original syscall; unrelated
writers are covered by the two-millisecond retry bound. No job completion,
guest-memory write, DMA order or FIFO command is synthesized or skipped.
In this build, `PS3_USLEEP_WAIT_SITES=0` disables the registered path.

Use `BUZZ_PPU_WORD_WAIT_TRACE` and `SPURS_JOB_TRACE_ALL=1` in a separate
diagnostic build to connect wait spans (115) and exact-word DMA publications
(116) with native SPURS job spans. `perf/ppu-word-dependencies.py` requires
zero dropped records, excludes incomplete capture-edge spans and leaves
unknown writers unmatched. Time inside a publishing job includes scheduling
and nested waits; it is not pure geometry CPU time. Measure actual gameplay
FPS and process CPU separately before changing defaults. A Linux synthetic
handoff benchmark or ARM64/QEMU correctness pass is not a Steam Deck/Android
device-performance or battery-life result.

`BUZZ_SPURS_POOL_AUTO_SPIN_US` (default `-1`, Android property
`buzzSpursPoolAutoSpinUs`) selects the automatic worker-pool poll on hosts
with at least 12 logical CPUs. Values 0..1000 are microseconds; `-1` retains
the SDK policy. Smaller hosts still park immediately. Explicit
`SPURS_POOL_SPIN_US` overrides the automatic policy, including values installed
by `BUZZ_SPURS_IDLE_SPIN_US`. This option does not change JTS polling, work
ordering, completion rules, or the number of workers. Qualify CPU cost and
presentation separately before adopting a shorter poll.

`BUZZ_SPURS_EXECUTION_TRACE` is diagnostic-only and defaults OFF. Event 117
wraps execution inside a SPURS job, recording image ID and scheduled thread
cycles on Windows or thread CPU nanoseconds on POSIX. A count of `UINT32_MAX`
means unavailable/overflow, not a measured duration. Event 118 identifies
blocking SPU channel waits by native thread ID. `perf/spurs-execution.py`
requires an explicit counter unit and zero lost records, excludes capture-edge
spans, and unions overlapping channel waits. Cycle density is not CPU
utilization; absent channel waits do not exclude polling, locks or preemption.
Neither execution wall time nor nested waits may be added to their parent
job/drain durations. Use trace-disabled builds for FPS comparisons.

`BUZZ_RENDERER_PACKED_FUSION` (default OFF, Android property
`buzzPackedIndexFusion`) changes the implementation selected by index fusion.
It needs `BUZZ_INDEX_FUSION=1` at runtime or `BUZZ_RENDERER_INDEX_FUSION=ON`
as the build default. Only fully mapped, restart-free u16 batches with a total of
4..65536 indices and remapping allowed qualify; other draws keep the existing
path. Multiple packets retain their order, gaps and overlapping ranges. A private 256 KiB table combines a 16-bit generation and first-use
ordinal. It is cleared on generation wrap and never caches guest data between
draws. Allocation failures fall back before any reference is emitted. The
offline CPU suite checks exact order/maps, all 65536 unique indices, SDK
interleaving, generation wrap, allocation failures and guarded source tails.
Measure RSX CPU and complete frame throughput separately before enabling it.

`BUZZ_GEOMETRY_DEFERRED_CLIP` (default OFF, Android property
`buzzGeometryDeferredClip`) aggregates exceptional-result checks across each
eight-vertex projection. It preserves each arithmetic operation, rounding mode
and zero canonicalization. If any intermediate reaches the original repair
threshold, the entire block is discarded and recomputed through the exact
per-operation path before output is stored. No guest-data cache is introduced.
The offline CPU suite compares ordinary, exceptional and random-bit inputs;
the captured clip oracle additionally checks against the original lifted code.
An isolated projection speedup is not evidence of higher game FPS. Qualify
this option independently on ARM and x86 before enabling a platform default.

`BUZZ_GEOMETRY_LAZY_ATTRIBUTES` (default OFF, Android property
`buzzGeometryLazyAttributes`) defers rigid/unskinned normal and UV preparation
until position-based culling retains triangles. Weighted palette blending keeps
its existing single pass. Deferred unit selectors still undergo validation;
invalid streams fall back as before. A pending-field mask prevents a partial
host bank from publishing stale attributes. Surviving jobs materialize and
transform every remaining field before ordinary vertex/tail publication.
This changes neither DMA, completion, publication ordering nor guest bounds.
Captured final index/vertex bytes are compared with the eager pipeline, with
coarse rejection OFF and ON, using `geometry-lazy-attributes-test.cpp` in the
offline CPU suite's `--corpus` mode. Measure game FPS separately from kernel cost.

`BUZZ_IMAGE39_NATIVE_REQUEST` (default OFF, Android property
`buzzNativeImage39Request`) replaces the pinned EU 1.00 descriptor request
with a host cache/output plan and one real MFC command for a miss. Regenerate
`tools/codegen/generate-image39-native.py` and, when selected,
`generate-image39-kernels.py` before configuring ON. CMake rejects an old or
different adapter instead of silently enabling an absent hook. Android also
requires `buzzNeonJobFp`; reference29 keeps the new property false.
The Windows build script exposes `-NativeImage39 -NativeImage39Request` and
explicitly resets the new CMake option OFF when that experiment is omitted.

`BUZZ_IMAGE39_REQUEST=0` retains the original request in an enabled build.
`BUZZ_IMAGE39_REQUEST_STATS=1` counts attempted/native requests and is not a
timing setting. Full-job replay must preserve ordered transfers and exit
controls; keep captured read-input divergences separate from exact cases.
The standalone oracle can be included in the offline CPU suite with
`--image39-request-corpus <B39REQ01-directory> --image39-source <original-C0BC.c>`.
It verifies the original source hash, retained full LS/register output, live
native results, GET arguments/payloads and fallback before mutation. DMA is
modeled there; a complete game/replay test still exercises the actual engine.

## Keeping documentation useful

Store run artifacts under ignored `analysis/`, with a compact table and
decision when needed. Keep code and a durable description of its behavior in
Git; completed session logs, phase trackers and benchmark narratives do not
belong in the maintained documentation. Commit each validated increment.
