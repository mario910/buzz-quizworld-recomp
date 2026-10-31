# Native geometry phase 3: implementation and evidence

Status: **in progress**, 2026-10-31. Phase 2 ended at `ef906be`, with default
geometry mask 827 (`0x33B`). The continuing objective in
[native-geometry-plan.md](native-geometry-plan.md) authorizes advancing through
subsequent phases after their gates, with strict Stage F reconstruction last.
The outer driver is native; the complete native data path remains open.

Native payload implementations now exist for all nine descriptor storage
families, plus the primary raw float3 path. Remapped weighted scatter also
has a complete native implementation with strict real-corpus/live coverage.
The four cold typed families still have open real-data/live gates, and
enclosing pipeline bodies keep phase 3 open.
Additional-stream BF8 orchestration is also native opt-in with strict
corpus/live checks. Its `0x7118/7298` run-list data decoder now has a native
opt-in implementation; its separate validation is recorded below.
The custom `0x93F0` dispatcher also has a native implementation through its
exact descriptor tail handoff; built-in input formats 1..5 remain separate.
The `0x9F8` skin/rigid controller now has native opt-in control through its
exact weighted-data/normalization tail or return boundary. `0xAF18` now has
native palette/frame/batch/dispatch/publication control, separately switched.
Rigid, cofactor and all three normalized direction variants now have native
implementations, independently switched below, with no observed real/live
coverage of those cold data paths. The AF18 data implementation inventory is
complete; cold coverage and final validation/default gates remain open.
The complete B880 weighted-data replacement passes strict real replay/live
and native rendering below; final performance/default gates remain open.

The next complete decoder, `AEA0/7390`, now has a separate
[implementation contract and real-input audit](native-geometry-index-audit.md).
The original tracer passes all 19,508 jobs/58,195 transfers; a scalar delta
model matches one complete captured register/LS pair. This is preparation for
native implementation, with all new decoder acceptance/default gates open.
The complete `7730/7770` delta region is now native opt-in, independently
checked and documented below. Other regions of the decoder remain unfinished.

Functional scene runs use forced shutdown. Their periodic successful native
and strict counters are lower bounds; final fallback totals are unavailable.
Historical statements that no fallback appears in a live log mean none was
reported, not a measured zero live fallback total. Completed corpus replay
provides exact native/fallback totals. The final default decision must retain
this distinction and use suitable instrumentation for live fallback totals.
The later rigid-skin build adds a flushed first-fallback notification per
stage; it does not retroactively add instrumentation to older frozen runs.

## Driver boundary audit

The registered image-41 workload starts at `0x0010` and reaches its main
driver at `0x1C18`. The driver interval `0x1C18..0x2718` contains 29 translated
blocks and 19 distinct direct callees. Its setup calls `0x08A8`; its control
flow includes descriptor-dependent branches, pipeline calls and an epilogue
that restores saved registers. Computed calls/returns need runtime evidence;
the static inventory alone does not prove coverage or native eligibility.

Reproduce the pinned inventory with:

```powershell
.\toolenv\Scripts\python.exe tools/codegen/inspect-geometry-driver.py
```

Private output: `analysis/native-geometry/phase3/driver-audit.json`. The tool
requires the original lift SHA256
`5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812`.
Original lifted sources and assets are unchanged.

## Shadow I/O comparison core

The driver issues DMA and atomics. The existing synchronous leaf checker
must not execute the whole job twice against real guest memory: that would
publish outputs and advance allocation state twice.

`port/native_geometry_job_io.h` implements the replay portion of a channel
transcript. The retained execution supplies the observed channel operations,
MFC state, list-input bytes and transfer payloads. A shadow execution must
issue the same channel operations/arguments and start each operation with
the same architectural controls. It receives the recorded GET/GETLLAR data;
PUT and all atomic-store attempts compare their full payload, including
padding, without accessing guest RAM. List-command input bytes plus exact
MFC arguments establish the same transfer shape/order. Transfers replay in
order, preserving overlapping LS destinations. Missing/extra operations,
changed inputs, control fields, output bytes and invalid LS ranges fail.
The controller must establish trace recordability before starting a shadow;
a mismatch in a supported shadow execution is a comparison failure, never
an eligibility fallback or a successful check.

Channel effects are applied only after validating the before-state. PC,
GPRs, other LS bytes, host MFC pointers, clocks and locks are not restored
from an operation. Final PC, all 128 GPRs, all LS and architectural controls
still require the existing full-state comparison; finishing the transcript
alone is not a whole-job pass. The Stage F exception is not expanded.

Synthetic validation:

```powershell
.\tests\scripts\test-geometry-job-io.ps1
```

PASS: ordered overlapping GETs followed by a 128-byte atomic output give an
exact complete exit state and only one guest commit. All six corpus transfer
kinds are exercised. Eleven corrupted operation/order/argument/control/list/
range cases are rejected, along with missing/extra calls and a corrupted
output tail. Separate checks reject dead exit GPR, unused LS and final-PC
differences even when I/O is complete. Read/count results and logical channel
queue rotation are preserved; applying channel state leaves host pointers,
PC, GPRs and unrelated LS untouched. Channel snapshots contain queue data
only, excluding `spu_channel::lock`; the test preserves five distinct lock
values across state application. Private log:
`analysis/native-geometry/phase3/job-io-test.log`.

## Integrated reference/shadow audit

The phase-3 adapter now connects this core to the real driver. It wraps 102
channel-write, 22 channel-read and one channel-count sites in the pinned
image-41 source. Corpus capture/replay observers forward DMA events to the
audit without losing their own observations. Shadow computed branches and
drain steps must stay in the compiled image; they cannot fall through to
unwrapped/interpreted code. Normal execution retains the original dispatch
and return behavior.

`BUZZ_GEOMETRY_JOB_AUDIT=1` opts into **reference versus shadow reference**,
not a native driver. `BUZZ_GEOMETRY_JOB_AUDIT_MAX` defaults to 512 attempts;
0 audits the full corpus. An unavailable adapter rejects the audit request.
External LS, unsupported entry state, asynchronous DMA, unknown reference
branches and unstable external inputs do not count as successful checks.
The owner token is atomic, and a foreign observer cannot write the recording
thread's transcript vectors. All comparison mismatches fail immediately.

The reference result is frozen at the driver exit boundary before the shadow
runs. Later peer changes to the retained context's events/reservations or
queues remain intact; comparing the live context at a later time would mix
different observations. External control/channel changes during recording
make the trace unrecordable before the trial. No architectural exit field is
waived to accommodate them. Host channel locks are never restored.

Build and controller tests:

```powershell
.\tests\scripts\build-geometry-phase3.ps1
.\tests\scripts\test-geometry-job-runtime.ps1
```

The controller passes 15 scenarios: exact execution, original external-LS
eligibility, asynchronous/unknown reference paths, unstable events/queues,
post-exit event/queue changes, changed output/exit/register/control state,
missing/extra operations and an unknown shadow step. Wrong supported shadows
abort with a difference diagnostic while the synthetic real output is still
committed exactly once. Fallback/unrecordable cases cannot satisfy the
nonzero-comparison gate. Private test log: `phase3/job-runtime-test.log`.

| Frozen adapter configuration | Corpus result | Whole-driver checks |
|---|---|---|
| Audit on, geometry checked, Stage F checked | 19,508 exact / 58,195 transfers, zero input changes or failures | 19,508 exact; 7,231 multi-transfer and 12,277 single-transfer jobs; zero skipped/failed |
| Audit on, geometry native, Stage F checked | same | same |
| Audit off, geometry native, Stage F checked | same | audit disabled; reference driver retained |

All use the phase-2 default mask 827, without a mask override. Logs are
`phase3/io-corpus-{mode2,mode1,off}.log` under `analysis/native-geometry/`.
These reproduce the existing driver; they do not validate a new native flow.
Stage F retains its previously accepted baseline contract.

A separate 60 s live menu run uses actual-native Stage F and geometry groups,
the same frozen adapter and a fresh private save. It records **256 strict
whole-driver + I/O matches**, no differences, and completes normally:
`analysis/vk-scene-p3-io-live.stderr.log`. The audit remains opt-in; the
phase-2 default mask is unchanged. This functional run is not an FPS claim.

`analysis/native-geometry/phase3/io-build.json` binds source hashes, the
preserved generated C, map and frozen `build-vulkan-phase3-io` EXE SHA256
`004919f6fc19a06fdfaf7d98e1840d86add41e52fac7a07c29d8b96ab6108b5c`.
Original game-save and lifted-source hashes are unchanged. Preliminary audit
builds/logs are superseded by this frozen set.

## Native input model

`port/native_geometry_job_input.h` decodes the reviewed 21-input descriptor
family into host values before any mutation. It checks the context/descriptor
ABI, LS ownership, declared region arithmetic, stack frame, staged input
addresses and configuration alignment. Rejection leaves both the SPU context
and the caller's output object unchanged. This reader is not the complete
native-path eligibility filter: codec/pipeline support still needs its own
decision before native execution.

```powershell
.\tests\scripts\test-geometry-job-input.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
```

PASS: **19,508 real entries**, **256 synthetic cases**, and **14 unchanged-state
rejections**. A source-pinned private oracle executes the original DMA-free
`0x08A8` setup and checks all 21 input pointers, including empty inputs and
every EA low nibble, plus preserved pointer-table padding. Reading the host
view leaves the entire input context byte-for-byte unchanged. Log:
`analysis/native-geometry/phase3/job-input-test.log`.

Two driver details are now explicit: `0x2F80/0x30E8` allocates
`(config[0] & 15) + 1` float4 tables, and the reusable input workspace starts
at `io_lsa + input[0].size_flags`, using the whole first word of the command
entry. It does not start after the last staged input. Invalid command-prefix
ranges are rejected. The reader now feeds the experimental native setup path
described below; this does not complete the native pipeline.

## Semantic native context initialization

`port/native_geometry_context.h` initializes configuration, table pointers,
allocator bounds, DMA tags, counts, optional viewport/object data and custom
descriptor words using host values and direct memory operations. It retains
the host configuration/table view for later pipeline stages and preserves
every unnamed LS byte. Invalid, wrapping, unaligned or context-aliasing source
ranges are rejected before any mutation or publication of host state.

```powershell
.\tests\scripts\test-geometry-context.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
```

PASS: **19,508 original driver preambles and initializer calls** produce the
same entire 256 KiB LS; **1,024 randomized synthetic initializer cases** cover
all 1..16 table counts, zero/boundary/maximum vertex counts and all optional
viewport/object/custom combinations; **13 rejection cases** preserve the
complete context and output object. The oracle is source-pinned and executes
the original preamble up to the `0x2F80` call, so actual arguments and custom
stack data come from the lift independently of the native model. None of the
real preambles issues the optional `0x24F0` metadata GET. Private log:
`analysis/native-geometry/phase3/context-test.log`.

This is an internal native-driver operation, not a callable replacement for
the original `0x2F80` SPU ABI. It leaves GPRs and controls unchanged; a future
native driver must supply its callees' inputs and reproduce the strict full
job exit/IO state. No temporary-register exception has been added. The helper
is now selected inside the experimental setup path. Table pointers are
initialized even when their implied payload would exceed scratch, as in the original setup;
whole-pipeline eligibility must prove payload capacity before decoding.

## Native driver preamble

`port/native_geometry_job_setup.h` now prepares the complete reviewed preamble
in C++, without dispatching translated blocks: save the caller's frame,
publish the 21 input pointers and setup quadwords, stage default/object
records, construct the custom descriptor stack block and apply the
position-only output-descriptor edits. Its host result carries the input view
and the initializer's arguments. It rejects the optional metadata-GET path,
the driver's STOP flag and unsupported source sizes/alignments before writes.
This eligibility is for the preamble only; remaining pipeline/output support
must be decided before selecting the whole native driver.

The same `test-geometry-context.ps1 -Corpus ...` command now compares the
native preamble's **entire LS** with the original driver's pre-initializer
state, then compares native context initialization. All **19,508 real jobs**
pass both comparisons. An additional **512 synthetic preambles** cover all
combinations of the six relevant flag bits, present/empty stream-B input and
command-header pipeline selections (absent, 2304, 2305 and unknown),
with randomized initial LS, padding and GPR words. **Seven additional rejected
preambles** leave the entire context and host result unchanged. The prior
1,024 initializer cases and 13 context rejection cases still pass.

`port/native_geometry_job_call.h` materializes the first call's complete
argument vectors, stack/link and nonvolatile GPRs 80..109, preserving 126.
Those vectors match the independent original preamble on the full corpus and
all 512 cases; the helper changes no LS, controls or unrelated GPRs. The native
initializer omits internal dead temporary GPR computation. Its caller must
still pass the complete strict whole-job check, without any field waiver.

## Native setup milestone (`39ba191`)

The following setup-only configuration is historical evidence. The current
outer-driver implementation and frozen build are documented below.

`BUZZ_GEOMETRY_JOB` is independent of the leaf-kernel switches:

| Value | Execution |
|---|---|
| 0 (default) | Retain the original driver; optional `BUZZ_GEOMETRY_JOB_AUDIT=1` still audits reference versus reference |
| 1 | Native C++ preamble, call-register boundary and context initialization; join the retained pipeline at `0x2088` |
| 2 | Run the original job once with real I/O, replay the same inputs into the native setup flow, and compare strict whole-job exit and I/O; retain original state |

Mode 2 checks every eligible job, without the reference-audit attempt cap.
The diagnostic AUDIT option and nonzero JOB mode are mutually exclusive.
An unavailable native adapter rejects a nonzero mode. Entry eligibility is
checked on the original context before any mutation or clone; unsupported
metadata-GET/STOP/source families retain the complete original flow. Unknown
shadow steps and every supported comparison mismatch abort, never fall back
or satisfy coverage. Unrecordable reference input changes do not count as
successful native comparisons.

The controller passes **43 scenarios**: 20 reference-audit
scenarios, 21 native checked scenarios (including unsupported original entry,
external LS, asynchronous/unstable inputs and deliberately wrong shadows),
actual-native dispatch and disabled dispatch. Tests establish distinct
reference/native callback selection, exactly one real guest output and
nonzero-check accounting. Command:
`tests/scripts/test-geometry-job-runtime.ps1`.

On frozen `build-vulkan-phase3-setup-snapshot`, all three modes pass the complete corpus:

| Driver / leaf configuration | Result |
|---|---|
| JOB=0, GEOMETRY=1, STAGEF=2 | 19,508 exact jobs / 58,195 transfers; retained driver, zero input divergence/failures |
| JOB=2, GEOMETRY=2, STAGEF=2 | 19,508 exact jobs / 58,195 transfers; 19,508 strict setup-flow checks, zero fallback/differences/input divergence/failures |
| JOB=1, GEOMETRY=1, STAGEF=2 | 19,508 exact jobs / 58,195 transfers; 19,508 actual setup-flow executions, zero fallback/input divergence/failures |

All keep the default leaf mask 827. Private logs are
`phase3/setup-snapshot-corpus-mode{0,1,2}.log`; `phase3/setup-snapshot-build.json` binds the frozen
EXE/map, preserved generated C and native source hashes. Stage F checked mode
retains its established baseline state; its final strict cleanup is unchanged.

The setup milestone's frozen EXE SHA256 is
`d1e8731c742137eb95e7435339d7600bebb915b7bf9a5b16f43211fca8f576da`.
The native `native_geometry_job_flow.cpp` module uses the reviewed SPU
optimization/ISA flags. The controller and its pre-main constructors remain
on the baseline ISA, before the application's CPU checks. Actual mode 1
preserves ordinary trampoline scheduling. Paired checks drain the job up to
its saved link without adding an artificial host frame; controller tests
assert the original zero entry depth. Earlier builds are preliminary.

Two 65 s live runs on this final build use native geometry groups, native
Stage F and fresh private saves. The unlimited reference/reference audit logs
**at least 445,000 strict matches**; JOB=2 logs **at least 450,000 strict
native setup-flow matches**, with rendering through CSV elapsed time 64.4 s
in both runs. No comparison differences occur. Externally changed reference
inputs are rejected before the trial and are not counted as matches. Logs:
`analysis/vk-scene-p3-snapshot-{audit,native-checked}.{csv,stderr.log}`.
These are functional checks, not FPS/CPU measurements or visual frame gates.

A separate JOB=1 run on the same frozen EXE also completes 65 s with native
Stage F and geometry groups, retaining renderer output through elapsed time
**64.7 s** (2,844 recorded frames) and no driver difference or HALT diagnostic:
`analysis/vk-scene-p3-snapshot-native-actual.{csv,stderr.log}`. Original player
save, private save-template and lifted-source hashes remain unchanged.

### Live reservation race and scene-runner correction

Unlimited checking initially aborted near the first menu in both the
reference/reference and native setup runs. An unbuffered reproduction showed
`resv_valid` differing at `0x3D34`. Recording did not check external changes
before the first channel, and later checked live controls separately from
capturing them. A peer could invalidate a reservation between those reads.

The recorder now retains entry controls and checks the exact frozen
before-snapshot against entry or the preceding after-snapshot. Unstable
reference input is marked unrecordable before starting a shadow. The checker
still rejects every supported shadow control difference; negative tests alter
the first shadow event/reservation state and require an abort with only one
real guest output. New tests also cover event, reservation and queue changes
before the reference's first channel. No control field is excluded or restored
to hide a native mismatch.

The scene runner now holds a process handle, detects early exit and reports
failure while retaining the log. Previously it could wait out the requested
duration after the game had already aborted. Difference diagnostics flush
stderr before abort so buffered live logging cannot lose the reason. The
early-exit reproduction fails as expected; the corrected unlimited live runs
complete their full duration and continue rendering past the prior failure.

## Native outer driver

`port/native_geometry_pipeline.h` now owns the main driver's complete outer
control in C++: output initialization, attribute decode and optional secondary
stream blend, skin/index/transform calls, visible-count decisions, output
allocation, vertex/index publication, commands and caller restoration.
Configuration, palette inputs/counts and output metadata are retained in host
values across stages. No original main-driver block is dispatched as a native
continuation. The generated adapter supplies a table of 14 reviewed library
entry points and retains their public wrappers and per-kernel switches.

This is an outer-control replacement, with the remaining Edge library still
using its SPU ABI and LS vertex tables. It does not yet provide the complete
host-buffer data path required to finish phase 3. The current scope is:

| Segment | Current implementation |
|---|---|
| Input setup and context initialization | Native C++ with host input/configuration/table state |
| Output initialization (`0x1AD8`) | Native null-metadata path; metadata GET descriptors rejected before writes |
| Attribute/secondary-stream decisions | Native control and SPU-exact RTZ weight calculation; library wrappers retained |
| Palette counts, skin/index/transform sequencing | Host counts/control, exact nonvolatile spill vectors; library wrappers retained |
| Empty/allocation/output/command decisions | Native control, ring getter and cached metadata; output library retained |
| Caller restoration | Native full-vector restore and ordinary return |
| Vertex data between library stages | LS/SPU-ABI boundaries remain; native data-path integration open |

Full u128 nonvolatile values matter even where only one lane controls a
branch: downstream callees spill complete vectors into observable LS. The
native flow reconstructs those values without adding checker exceptions.
The output-allocation branch checks the low byte, matching the original even
for a result such as `0x100`. Unsupported metadata/STOP/source families keep
the whole original fallback at entry. The driver switch remains default off.

The adapter's function-table ABI is version 2; CMake rejects stale setup-only
generated adapters. The native flow translation unit now uses the integer and
floating-point SPU flags, including RTZ and the configured result guard, for
the blend-weight calculation. The baseline-ISA controller stays separate.

### Independent outer-control checks

```powershell
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
```

PASS: **1,024 synthetic paths and all 19,508 real entries**. A private,
source-pinned oracle executes 47 original driver/setup blocks against 14
explicit library mocks. It compares call order, links, complete argument and
nonvolatile vectors, legal callee spills and the complete final GPR/LS/control
state. Mocks clobber volatile registers independently of the native flow.
Cases cover all six relevant flags, empty/present secondary input, blend
weights 0/1/32768/65535, palette-byte carry/wrap boundaries, cull results and
allocation success versus a false low byte. This isolates driver decisions;
the real-library replay below supplies the separate integration proof.
Private log: `phase3/driver-flow-test.log`. The 43 controller scenarios also
pass with the new callback-table ABI.

### Frozen integration evidence

`phase3/outer-build.json` binds `build-vulkan-phase3-outer`, preserved generated
C, map and source hashes. EXE SHA256:
`e495626414809acdbbf0b684fbb611a2b910559f7df686c88dbb4043bd43f6c0`.

| Driver / leaf configuration | Complete image-41 corpus |
|---|---|
| JOB=0, GEOMETRY=1, STAGEF=2 | 19,508 exact / 58,195 transfers; zero input divergence/failures |
| JOB=1, GEOMETRY=1, STAGEF=2 | 19,508 actual native outer-flow executions, zero fallback; same exact corpus result |
| JOB=2, GEOMETRY=2, STAGEF=2 | 19,508 strict whole-job + I/O checks, zero fallback/differences; same exact corpus result |

All retain leaf mask 827 without an override. Logs:
`phase3/outer-corpus-mode{0,1,2}.log`. Stage F checked mode preserves its
accepted baseline; strict Stage F reconstruction remains final phase 7.

A 65 s menu run on this same frozen EXE with JOB=2, GEOMETRY=1 and STAGEF=1
records **at least 445,000 strict full-state + I/O matches**, with rendering
through CSV elapsed time 64.6 s and no differences or early exit. Externally
changed reference snapshots are unrecordable before trial and do not count
as successful checks. Log: `analysis/vk-scene-p3-outer-checked.stderr.log`.
This is functional evidence, not a performance measurement.

A separate JOB=1 run with actual-native Stage F and geometry completes menu
navigation and **30 s in the round**, starting the round at 137.4 s and
retaining rendered CSV output through 167.3 s (7,759 recorded frames), with
no early exit or driver difference/HALT diagnostic. It uses the same frozen
EXE and a fresh private save. Logs:
`analysis/vk-scene-p3-outer-round-actual.{csv,stderr.log}`. PrintWindow captures
from this run contain black client areas, so they do **not** satisfy the
visual gate; renderer readback dumps are still required. These functional
runs supply no FPS/CPU-benefit claim or unchanged-interpreter-count proof.
Original player-save, private template and original lifted-source hashes
remain unchanged.

## Native index publication (`0x2DC0`)

`port/native_geometry_publish.h` replaces the index-output stage with a C++
publication plan and a native chunk loop. Host values carry the source,
remaining bytes, CPU allocation cursor and GPU offset between transfers.
The implementation preserves the complete LS spill footprint, output-record
padding, allocation synchronization fields, interrupt state and all exit
vectors. It sends the same ordered PUT operations in chunks of at most
16 KiB through the existing single-commit shadow-I/O adapter.

`BUZZ_GEOMETRY_JOB_PUBLISH=1` enables this stage inside JOB=1/2; its default
is **0** independently of the whole-driver and leaf switches. A read-only
admission check rejects unsupported ownership, ranges, aliases, allocation
capacity or enabled-interrupt continuation before the stage makes a write or
channel call. Rejected stage inputs use the original publisher, with separate
native/fallback counters; they do not count as native publication coverage.
The zero-count path retains its original spill/return behavior and issues no
channel operation. The remaining vertex publisher is unchanged.

```powershell
.\tests\scripts\test-geometry-publish.ps1
```

PASS: **2,048 synthetic full-state/channel/payload cases** against ten pinned
original blocks, plus **nine unchanged-state rejections**. Cases cover zero
indices, rounding to eight indices, both sides of 16 KiB chunk boundaries,
multiple chunks, 32-bit count/address wrap and arbitrary non-preferred lanes.
Every final GPR/LS/control field is compared, along with channel order,
arguments, controls before/after each operation and every transfer byte.
Private log: `phase3/publish-test.log`. The outer-flow model test and all
43 controller scenarios also pass after integration.

The frozen `build-vulkan-phase3-publish` EXE SHA256 is
`c99ba1bb48a8083352e78bc4d35745bd437cceb5a9548093805a90cbbf04a290`;
`phase3/publish-build.json` binds its map, generated C and native sources.
On that same EXE, all three driver modes pass **19,508 exact jobs / 58,195
transfers**, with zero input divergence/failures. JOB=1 and JOB=2 each execute
**7,201 native index publications, zero publication fallback**; JOB=2 also
passes all 19,508 strict whole-job + I/O checks. Logs:
`phase3/publish-corpus-mode{0,1,2}.log`. The original driver arm disables the
new switch; both native arms enable it. Stage F remains in baseline-preserving
checked mode and the leaf mask remains 827. Diagnostic replay timings are
not a cycle/FPS benefit claim.

Three functional runs on this frozen publisher EXE enable the new switch,
actual-native Stage F and the default geometry mask, using fresh private
saves. All complete normally with no difference/HALT diagnostic:

| Run | Evidence |
|---|---|
| JOB=2 menu, 65 s | At least 445,000 strict whole-job + I/O matches, including at least 370,000 native index-publication executions; renderer continues through 64.7 s |
| JOB=1 menu, 65 s | Renderer continues through 64.6 s; normal renderer readback at frame 2,500 |
| JOB=1 round, 30 s after navigation | Round start at 147.1 s; renderer continues through 177.0 s (8,213 recorded frames); normal round-introduction readback at frame 6,500 |

Logs: `analysis/vk-scene-p3-publish-{checked,menu,round}.{csv,stderr.log}`.
Native renderer readbacks, inspected as 1280x720 PNGs, are
`phase3/p3-publish-{menu,round}.{ppm,png}`. These replace the failed
PrintWindow method for this increment's visual checks. Menu geometry and the
round introduction's character, environment and text look normal against the
phase-2 baseline scenes. The screenshots are visual checks, not a claim of
pixel equality between different animation frames. Original save/template
and source pins remain unchanged. Final reversed FPS/CPU measurements and
interpreter-count evidence remain open, and the publication switch stays off
by default.

## Shared native output layout and vertex publication

`port/native_geometry_output.h` provides a host description shared by buffer
sizing (`0x2B80`) and vertex publication (`0x35F0`). It decodes the vertex
count, four reviewed built-in formats (strides 12/20/24/24) and the custom
descriptor stride, while preserving the full vectors needed at the SPU ABI.
The native size operation computes the combined index/vertex allocation with
the original 128-byte rounding and 32-bit wrap behavior. The native vertex
publisher uses that layout and a host transfer cursor to issue the original
ordered, at-most-16-KiB PUT chunks, including the original zero-byte operation
when the vertex payload is empty. Its spill, padding, allocation-record,
synchronization-byte and complete exit state remain exact.

Two independent switches, both default **0**, select these stages inside
JOB=1/2:

| Option | Native operation |
|---|---|
| `BUZZ_GEOMETRY_JOB_LAYOUT=1` | Output size and a host layout retained across allocation, index publication and vertex encoding |
| `BUZZ_GEOMETRY_JOB_VERTEX=1` | Vertex publication, using the retained layout when its complete source snapshots still match |

They are independent of `BUZZ_GEOMETRY_JOB_PUBLISH`, which still selects index
publication. With only VERTEX enabled, publication reads its own layout. A
changed cached configuration, descriptor, callback vector or dispatch-table
quadword forces a fresh read; a cache hit cannot hide changed padding or
non-preferred lanes. Unsupported custom callbacks, built-in dispatch tables,
ranges, aliases or enabled-interrupt publication continuations retain the
original stage before a native spill/channel operation. Stage fallback has
separate counters and does not count as native stage coverage.

Native stages now receive their admitted plans directly within ordinary
host call frames, preserving the original return depth and continuation.
This also removes the index publisher's duplicate admission read. No
translated size/getter/publication block runs for these native operations;
other library callers of the original stride getter remain unchanged.

```powershell
.\tests\scripts\test-geometry-output.ps1
```

PASS: **2,048 size cases and 2,048 vertex-publication cases** against 17 pinned
original blocks, plus **20 no-mutation/no-I/O rejections**. Tests compare every
GPR, LS byte and control field, channel arguments/order/before-after controls
and each transfer byte. Coverage includes all four built-ins, custom and null
descriptors, all descriptor low nibbles, zero/boundary/multi-chunk payloads,
index/address wrap, arbitrary non-preferred lanes, and invalidation after
independent full-vector source changes. Private log: `phase3/output-test.log`.
The 2,048 index-publication tests, 43 controller scenarios and outer-flow model
(1,024 synthetic paths plus all 19,508 real entries) also pass.

The frozen `build-vulkan-phase3-output` EXE SHA256 is
`d0f4baeffdec4ca9f6d95a2d287fc1646765f49f7d3d5f0b3813a8d58053307e`;
`phase3/output-build.json` binds its native sources, map and preserved adapter.
All five corpus configurations below pass **19,508 exact jobs / 58,195
transfers**, with zero input divergence/failures and zero native-stage
fallback. Each checked arm passes all 19,508 strict whole-job + I/O checks.

| Arm | JOB / LAYOUT / VERTEX / PUBLISH | Native stages and host-layout reuse |
|---|---|---|
| Original driver | 0 / 0 / 0 / 0 | Retained driver baseline |
| Size independently checked | 2 / 1 / 0 / 1 | 7,201 sizes, 7,201 index publications |
| Vertex independently checked | 2 / 0 / 1 / 1 | 7,201 vertex and index publications; vertex stage reads its own layout |
| Combined actual native | 1 / 1 / 1 / 1 | 7,201 executions of each stage; all 7,201 vertex publications reuse the size stage's host layout |
| Combined checked | 2 / 1 / 1 / 1 | Same stage counts and reuse, with full job/I/O comparison |

Logs: `phase3/output-corpus-{off,layout,vertex,native,checked}.log`. All keep
the default leaf mask 827 and baseline-preserving Stage F checked mode.
The replay timings do not establish a measured CPU-cycle or FPS benefit;
the new switches remain off pending the required matched measurements.

Three functional runs on this same frozen output EXE enable LAYOUT, VERTEX
and PUBLISH together, with actual-native Stage F, leaf mask 827 and fresh
private saves. The 65 s checked menu run records **at least 450,000 strict
whole-job + I/O matches**, including **at least 375,000 native executions of
each new stage** and the same number of vertex-publication layout reuses.
Rendering continues through 64.5 s (2,097 recorded frames), with no difference
or early exit. As before, externally changed reference snapshots are rejected
before trial and do not count as successful comparisons.

An actual-native menu run completes 75 s, with rendering through 74.5 s
(3,150 recorded frames). A separate actual-native run reaches the round at
136.5 s and continues for another 30 s. Renderer readbacks at frames 2,500
(menu) and 6,500 (round introduction) were inspected as 1280x720 PNGs and look
normal: scene geometry, text and the round's transition effects render
correctly. Logs are `analysis/vk-scene-p3-output-{checked,menu,round}.{csv,stderr.log}`;
images are `phase3/p3-output-{menu,round}.{ppm,png}`. All runs complete normally,
with no driver difference/HALT diagnostic. Original saves, private template
and source pins remain unchanged. These functional runs make no performance
benefit claim and do not close the remaining interpreter/performance gates.

## Native command-buffer preparation (`0x32C8`)

`port/native_geometry_commands.h` now owns command-buffer reservation,
allocation-record scans, the optional fenced 4-byte synchronization GET and
the 32-byte command preamble (`0x1710`/`0x1714`). A host plan keeps the logical buffer limit
separate from the 128-byte allocator reservation. The packet condition is
whether any record is ready; its bytes and complete spill/exit state match
the original, including packets that extend past the record's logical limit
while remaining in the physically valid LS span. No new logical-capacity
restriction is imposed. Unsupported ranges/aliases or an enabled-interrupt
fenced-read continuation retain the original stage before writes or I/O.

`BUZZ_GEOMETRY_JOB_COMMANDS=1`, default **0**, selects this preparation stage
inside the native job. At this preparation checkpoint, final packet encoding, relocation and
publication remained in the library. The generated public `0x32C8` entry routes nested
calls as well as the outer empty-output call. Routing is restricted to the
native pipeline's current context: the retained JOB=2 reference and foreign
comparison contexts still execute the original function. A scoped owner is
restored on return. This increment introduced adapter protocol 3; the final
publication increment below advances the requirement to protocol 4. CMake
rejects stale adapters so a requested feature cannot silently be absent.

```powershell
.\tests\scripts\test-geometry-commands.ps1
```

PASS: **4,096 strict full-state/channel/fenced-GET cases**, against 14 pinned
original blocks, and **16 no-mutation/no-I/O rejections**. Tests cover empty
output, lists with up to 1,024 allocation records, the first missing synchronization record,
ready-record prefixes, no-ready/no-read paths, all word-aligned output-EA low
offsets, workspace low nibbles, zero and maximum logical command limits,
packet padding and arbitrary non-preferred lanes. Each GET's arguments,
controls before/after and received bytes are compared, as are all final
GPRs, LS bytes, controls and returns. Private log: `phase3/commands-test.log`.

The outer-flow model additionally proves routing: outside the native scope,
for a foreign context, with the option disabled, after scope exit and for an
unsupported original entry, the original callback runs exactly once. An
admitted owner executes the native operation instead. The 1,024 model paths,
all 19,508 real model entries and 43 controller scenarios also pass. Log:
`phase3/driver-flow-test.log`.

The frozen `build-vulkan-phase3-commands` EXE SHA256 is
`cee40b06dd7c8e694fa6e915f5ee7bf52e22865ecb2e88497233621f4e5af667`;
`phase3/commands-build.json` binds its sources, map and protocol-3 adapter.
Four complete corpus arms pass **19,508 exact jobs / 58,195 transfers**, with
zero input divergence/failures:

| Arm | Configuration and coverage |
|---|---|
| Original driver with COMMANDS requested | JOB=0: original result unchanged; the native-owner route is inactive |
| Independently checked preparation | JOB=2, COMMANDS=1, other new output switches 0: 19,508 native command-buffer preparations, zero fallback, 19,508 strict whole-job + I/O checks |
| Combined actual native | JOB=1, all new output switches 1: 19,508 native preparations; 7,201 each size/index/vertex stages and layout reuses; zero fallback |
| Combined checked | JOB=2, same switches: same stage counts plus all 19,508 strict whole-job + I/O checks |

Logs: `phase3/commands-corpus-{off,independent,native,checked}.log`. All retain
leaf mask 827 and Stage F's baseline-preserving checked mode. Preparation is
native for both empty and drawn output. These checks do not make final
command encoding/relocation/publication native, and their replay timings do
not establish FPS or CPU-cycle savings.

On this same frozen EXE, a 65 s checked menu run with all new output switches
enabled records **at least 450,000 strict whole-job + I/O matches and native
command-buffer preparations**. The earlier size/index/vertex stages each
execute at least 375,000 times, with the same number of layout reuses.
Rendering continues through 64.7 s (2,090 recorded frames). External reference
input changes are rejected before trial, without counting a native match.

Actual-native menu and round runs also complete normally: the 75 s menu run
renders through 74.6 s (3,154 frames); the round starts at 136.5 s and runs
for another 30 s, rendering through 166.6 s (7,747 frames). Renderer readbacks
at frames 2,500 and 6,500, inspected as 1280x720 PNGs, show normal menu and
round-introduction geometry, text, character and effects. Logs:
`analysis/vk-scene-p3-commands-{checked,menu,round}.{csv,stderr.log}`. Images:
`phase3/p3-commands-{menu,round}.{ppm,png}`. No difference/HALT or early-exit
diagnostic occurs. Original save, private template and lifted-source hashes
are unchanged. These are functional checks; final interpreter counts and
matched reversed FPS/CPU measurements remain open.

## Native final command relocation and publication (`0x27A0`)

`port/native_geometry_command_publish.h` now owns the final command data
path: append allocation-record semaphores (`0x1D6C`/`0x1D74`), split packets
at 128-byte boundaries, relocate method offsets for incrementing packets,
retain offsets for non-incrementing packets, fill unused command words with
NOPs and issue the fenced PUT (`0x22`). Host packet traversal replaces the
translated block dispatch. Overlapping physical tail shifts use `memmove`,
and padding uses bulk zeroing. ABI materialization retains all vector lanes,
physical padding bytes, controls, return state and channel effects exactly.

A read-only byte plan bounds semaphores, data-dependent insertions and actual
physical shifts before any mutation or I/O. Unsupported ranges, aliases,
shift constants or enabled-interrupt publication retain the original stage;
fallback is reported separately. The zero-destination return needs no LS or
DMA access. The planner does not require a terminating look-ahead to lie
inside the logical command allocation: the original loads it unconditionally,
using wrapped LS addressing, even when traversal has finished. The initial
bounded-look-ahead implementation passed replay but unnecessarily retained
1,407 original calls. The source audit identified that restriction, and the
implementation now reproduces the original load. This changes eligibility,
not the strict comparison contract.

`BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH=0/1`, default **0**, selects this stage
independently of preparation, output layout and index/vertex publication.
The public generated entry uses the existing context-specific native-owner
scope, including nested command-emitter calls. JOB=0, the retained JOB=2
reference, foreign contexts and disabled/unsupported calls keep the original
entry. The original entry retains its leaf switches, including the `0x28F0`
fill switch; enabling the complete native publication selects its own bulk
fill. This publication increment introduced adapter protocol **4**; the emitter
increment below advances the requirement to protocol **5**. Stale adapters
fail at configure.
No runtime default changes in this increment.

```powershell
.\tests\scripts\test-geometry-command-publish.ps1
```

PASS: **8,256 strict full-state/channel/fenced-PUT cases**, using 16 pinned
original blocks, plus **20 unchanged-state/no-I/O rejections**. Coverage
includes all word offsets within a 128-byte block, empty and zero-destination
paths, semaphores present/absent, incrementing and repeated-method packets,
4,107 cases with splits (3,227 with multiple splits), 1,088 exact-fit cases,
2,048 zero-destination returns, 64 wrapped terminating look-aheads, random
non-preferred lanes and physical tail bytes. Full GPRs, LS, controls, return,
ordered channel arguments and published payloads are compared. Private log:
`phase3/command-publish-test.log`. The owner/reference/foreign/disabled/
unsupported/post-scope routing test, all 1,024 outer-flow model scenarios,
19,508 real model entries and 43 controller scenarios also pass.

The frozen `build-vulkan-phase3-command-publish-v2` EXE SHA256 is
`17030504151dd10ae7f8a2c1c05243dde3cbf259b4a670f247cc4218615f8fe0`;
`phase3/command-publish-v2-build.json` binds the source hashes, map and
protocol-4 adapter. Four corpus arms each pass **19,508 exact jobs / 58,195
transfers**, with zero input divergence or failure:

| Arm | Configuration and native coverage |
|---|---|
| Original | JOB=0, COMMAND_PUBLISH=1: native-owner route inactive; original output exact |
| Independent publication | JOB=2, COMMAND_PUBLISH=1, preparation/other output switches 0: 19,508 native publications, zero fallback, 19,508 strict whole-job + I/O checks |
| Combined actual native | JOB=1, all new output switches 1: 19,508 native preparations/publications; 7,201 size/index/vertex stages and layout reuses; zero new-stage fallback |
| Combined checked | JOB=2, all new output switches 1: same stage counts and all 19,508 strict whole-job + I/O checks |

Logs: `phase3/command-publish-v2-corpus-{off,independent,native,checked}.log`.
All arms preserve leaf mask 827 and Stage F's baseline-retaining checked
mode. The exact native count also confirms that the retained JOB=2
reference is not being replaced by the new stage. Diagnostic replay timings
are not evidence of FPS or normalized CPU savings.

On this same frozen EXE, the 65 s checked menu run passes **at least 450,000
strict whole-job + I/O comparisons and native command preparations/
publications**. Size/index/vertex stages each execute at least 375,000 times,
with the same number of host-layout reuses. The CSV records 2,094 frames
through 64.553 s. These runs use actual native Stage F (`BUZZ_STAGEF=1`) and
geometry execution; the temporary Stage F contract is not widened.

Actual-native menu and round runs complete normally. The menu records 3,193
frames through 74.708 s. The round starts at 136.5 s and continues for 30 s,
recording 7,795 frames through 166.495 s. Renderer readbacks at frames 2,500
and 6,500, inspected as 1280x720 PNGs, show normal menu geometry/text/TVs and
round-introduction host, instructions and effects. Each run uses its own
fresh private save, background muted window and native buzzer mailbox;
no compilation, CPU tests or profiling overlap these runs. Logs:
`analysis/vk-scene-p3-command-publish-v2-{checked,menu,round}.{csv,stderr.log}`.
Images: `phase3/p3-command-publish-v2-{menu,round}.{ppm,png}`. No driver
DIFF/HALT/fallback diagnostic or early exit occurs. Original saves/template
and pinned lift remain unchanged. These functional checks do not establish
FPS or CPU savings; final interpreter counts and reversed same-EXE normalized
CPU/FPS gates remain open.

## Native position commands and shared binding packets (`0x02E8`, `0x0100`, `0x0020`)

`port/native_geometry_command_bindings.h` reads the output attribute list into
a host view and writes RSX format/address and reset packets directly. It owns
shader-slot selection, stride/type/component packing, GPU base-plus-offset
and location bits, with full vector ABI reconstruction. Binding and reset
share the same host list; reset preserves the original size-zero/type-2 word.
The read-only plans check actual touched quadwords and aliases before writes.
Empty or fully masked lists do not dereference their unused command record.
Even the lift's six-bit arithmetic-shift behavior for nonstandard slot bytes
is retained; synthetic tests include these bytes without claiming that they
represent valid rendered GPU formats.

`port/native_geometry_position_commands.h` now owns the complete position-only
emitter body: caller spills, optional raster/cull commands, cache invalidation,
index binding, primitive begin/end, leading alignment batch, full 8192-byte
packets, final batches, attribute reset and caller restoration. Host loops and
packet sizes replace translated block dispatch. Full packets generate their
batch words directly and reconstruct only the final iteration's scratch
vectors, preserving its original RMW snapshot. The native emitter calls the
existing preparation/publication entries, which retain their separate flags;
with all output flags enabled, those stages and the shared writers are native.
At this position-emitter checkpoint, `0x12F0` remained original; its shared
binding/reset calls could use the new native writers under the same owner
scope. The following increment replaces its body.

A read-only preflight computes the exact packet sizes and 128-byte insertion
space before preparation can issue a GET. It validates the combined root/
preparation stack footprint, attribute descriptors, allocation record,
metadata and command workspace. A nonzero destination must fit the logical
reservation after relocation; zero-destination calls still assemble commands
and may extend past that logical limit if their actual touched LS span fits.
Large unpublished streams therefore retain the original full-packet behavior.
Unsupported ranges/aliases/capacity or interrupt continuations retain the
original emitter before any mutation or I/O, and do not count as native.

`BUZZ_GEOMETRY_JOB_EMIT=0/1`, default **0**, controls this coupled emitter/
writer group. Public generated entries route only the native pipeline owner;
the JOB=2 retained reference and foreign contexts stay original, including
nested library calls. Counters distinguish binding, reset and position
emission. This increment introduced adapter protocol **5**; the full-emitter
increment below advances the requirement to protocol **6**. Older adapters
are rejected by CMake. The earlier preparation/publication and leaf switches retain their
behavior. JOB remains default 0 and the leaf default remains 827.

```powershell
.\tests\scripts\test-geometry-command-bindings.ps1
.\tests\scripts\test-geometry-position-commands.ps1
```

PASS: **8,192 strict full-state binding/reset cases** against 22 pinned blocks,
plus **28 unchanged-state rejections**. Counts 0 through 255, empty/masked
lists with invalid unused record pointers, all word offsets, duplicate and
nonstandard slots, full upper lanes, format/address arithmetic and random
LS padding are covered. Log: `phase3/command-bindings-test.log`.

PASS: **8,320 strict position-emitter state/channel cases** against 75 pinned
original blocks including its real callees, plus **26 unchanged-state/no-I/O
rejections**. Tests run the new emitter with original and native callees and
compare all GPRs/LS/controls/returns, ordered GET/PUT arguments, controls and
payloads. Coverage includes absent/skip/normal/reversed raster state,
all byte offsets for index alignment, 0/1/255/256/257 and larger index counts,
64 minimum-reservation zero-destination cases, 64 full-packet cases, varied
attribute masks, uninitialized padding and arbitrary non-preferred lanes.
Log: `phase3/position-commands-test.log`.

The command-preparation parameter-view extension also re-passes its 4,096
strict cases and 16 unchanged-state rejections. Owner/reference/foreign/
disabled/unsupported/post-scope routing, 1,024 outer-flow model scenarios,
19,508 real model entries and 43 controller scenarios pass. The root routing
probe deliberately rejects before consulting a null callback table, proving
that unsupported entry planning executes no callee or I/O.

The frozen `build-vulkan-phase3-position-commands` EXE SHA256 is
`4b48ade071ceecb7eaac6506b78987f6c3d436d85fa80646d6513beae0fd640a`;
`phase3/position-commands-build.json` binds its source/map/protocol-5 adapter.
Four corpus arms each pass **19,508 exact jobs / 58,195 transfers**, with
zero input divergences or failures:

| Arm | Configuration and native coverage |
|---|---|
| Original | JOB=0, EMIT=1: native-owner route inactive; original result unchanged |
| Independent emitter/writers | JOB=2, EMIT=1, all older output switches 0: 2,123 native position emissions, 7,201 native bindings and resets, zero fallback; all 19,508 strict whole-job/I/O checks pass |
| Combined actual native | JOB=1, all new output switches 1: same emitter/writer counts; 19,508 preparations/publications, 7,201 size/index/vertex stages and layout reuses; zero new-stage fallback |
| Combined checked | JOB=2, all new output switches 1: same native counts and all 19,508 strict whole-job/I/O checks pass |

Logs: `phase3/position-commands-corpus-{off,independent,native,checked}.log`.
Leaf mask remains 827, with Stage F in baseline-retaining checked mode. Exact
stage counts confirm the retained JOB=2 reference stays original. These replay
timings do not establish FPS or normalized CPU savings.

On the same frozen EXE, the 65 s checked menu run passes **at least 450,000
strict whole-job + I/O comparisons**, including **120,000 native position
emissions** and **375,000 native bindings and resets each**. Preparation/
publication each record at least 450,000; size/index/vertex stages and layout
reuses each record at least 375,000. The CSV records 2,096 frames through
64.971 s. Actual native Stage F and geometry execution are used
(`BUZZ_STAGEF=1`, `BUZZ_GEOMETRY=1`); no baseline exception is expanded.

Actual-native menu and round runs complete normally. The 75 s menu run
records 3,161 frames through 74.564 s. The round starts at 136.5 s and
continues another 30 s, recording 7,722 frames through
166.550 s. Renderer readbacks at frames 2,500 and 6,500, inspected as
1280x720 PNGs, show normal menu geometry/text/TVs and round-introduction
host, instructions and effects. These runs use separate fresh private saves,
muted background windows and the native buzzer mailbox, with no compilation,
CPU tests or profiling overlapping them. Logs:
`analysis/vk-scene-p3-position-commands-{checked,menu,round}.{csv,stderr.log}`.
Images: `phase3/p3-position-commands-{menu,round}.{ppm,png}`. No driver
DIFF/HALT/fallback or early-exit diagnostic occurs. These are functional
checks, without a performance claim or default-on decision; final interpreter
counts and reversed same-EXE normalized CPU/FPS gates remain open.

## Native full multipass command emitter (`0x12F0`)

`port/native_geometry_full_commands.h` now owns the full emitter body and
keeps the pass list, state fields and packet-space calculation in host views.
It handles all four optional state groups, marker-8 skips, raster/cull
selection, repeated indexed draws, leading alignment batches, full 8192-byte
packets, final batches and caller spill/restoration. Large packet words are
generated directly; only the last loop iteration's surviving ABI/RMW state
is reconstructed. The shared binding/reset, reservation and publication
stages retain their independent switches. With all command switches enabled,
the captured command subsystem executes natively.

The command-template header gives the pass count; records are 64 bytes each
at header `+16`. Four state-group disable bits are selected from word 1 of
the full flags vector by the original GB/CGTI sequence, rather than from its
preferred word. The host plan preserves that selection while reconstructing
all lanes of the original masks. Repeated passes draw the same index range;
index-count validation applies per pass. A null template still performs the
original initial LS-zero quadword load for ABI state and selects one pass
without optional state fields.

A read-only preflight validates pass count/descriptor spans, the combined
root/preparation stack footprint, metadata/command aliases and exact packet
expansion before the first possible synchronization GET. Published streams
must fit after 128-byte relocation; unpublished zero-destination streams use
physical LS bounds and retain full-packet emission beyond the logical
reservation. Unsupported ranges, capacity or interrupt continuations keep
the original emitter before mutation/I/O, without counting fallback as native.

`BUZZ_GEOMETRY_JOB_FULL_COMMANDS=0/1`, default **0**, selects this function
independently of the earlier EMIT group and preparation/publication flags.
Owner-scoped generated routing leaves the retained JOB=2 reference and
foreign contexts original. Adapter protocol **6** is required; stale
adapters fail at configure. No default or checker exception changes.

```powershell
.\tests\scripts\test-geometry-full-commands.ps1
```

PASS: **8,512 strict full-state/channel cases** with original and native
callees, plus **32 unchanged-state/no-I/O rejections**. The pinned oracle has
74 unchanged original blocks including the real callees. The reachable root
has **22 blocks**: the earlier structural inventory's 24 included two blocks
from the dead lexical tail after an unconditional transfer. The original
text remains in the oracle, with an aborting stub at that unreachable
boundary; no reachable behavior or state comparison is excluded.

Coverage includes all 16 group-mask combinations with independent template
presence/word offsets, skip markers and both raster orders, varied index
alignments/counts, full packets, 128 tight reservations, 64 minimum-reservation
zero destinations and 64 larger 16-pass unpublished streams. Arbitrary upper
lanes, input fields and physical padding are retained. Tests also independently
parse each published packet stream and compare its final non-NOP packet end
with the planned relocated size; unpublished cursors must equal the planned
raw size. This checks the planner separately from byte/state equivalence.
Private log: `phase3/full-commands-test.log`.

Owner/reference/foreign/disabled/unsupported/post-scope routing, 1,024 outer
model paths, all 19,508 real model entries and 43 controller scenarios pass.
The new root's rejection probe uses a null callback table to ensure no callee
or I/O is reached before unsupported-entry fallback.

The frozen `build-vulkan-phase3-full-commands` EXE SHA256 is
`58e5bec3df21b3f2e3843e165b7a5ec6626f83c82e3ef7b140cd44491d5738bf`;
`phase3/full-commands-build.json` binds its sources, map and protocol-6
adapter. Four corpus arms each pass **19,508 exact jobs / 58,195 transfers**,
with zero input divergence/failure:

| Arm | Configuration and native coverage |
|---|---|
| Original | JOB=0, FULL_COMMANDS=1: native-owner route inactive; original result unchanged |
| Independent full emitter | JOB=2, FULL_COMMANDS=1, all older output switches 0: 5,078 native full emissions, zero fallback; all 19,508 strict whole-job/I/O checks pass |
| Combined actual native | JOB=1, all new output switches 1: 5,078 full and 2,123 position emissions; 7,201 bindings/resets; 19,508 preparations/publications; 7,201 size/index/vertex stages and layout reuses; zero new-stage fallback |
| Combined checked | JOB=2, same switches: same native counts plus all 19,508 strict whole-job/I/O checks |

Logs: `phase3/full-commands-corpus-{off,independent,native,checked}.log`.
Leaf mask remains 827 with Stage F's accepted baseline-retaining checked
contract. Native counts also prove the retained JOB=2 reference is not
replaced. This closes the captured command-stage implementation inventory;
it does not finish the remaining geometry data stages or prove a CPU/FPS
benefit from diagnostic replay timings.

On the same frozen EXE, the 65 s checked menu run passes **at least 445,000
strict whole-job + I/O comparisons**, including **250,000 native full-emitter
calls**, 120,000 position-emitter calls and 370,000 binding/reset calls each.
Preparation/publication each record at least 445,000 executions; size/index/
vertex stages and layout reuses each record at least 370,000. The CSV has
2,090 frames through 64.933 s. Actual native Stage F and geometry execution
are used (`BUZZ_STAGEF=1`, `BUZZ_GEOMETRY=1`); the existing baseline exception
is not expanded.

Actual-native menu and round runs complete normally: the menu records
3,153 frames through 74.630 s; the round starts at 136.4 s and continues
for 30 s, recording 7,734 frames through 166.402 s. Renderer readbacks at
frames 2,500 and 6,500, inspected as 1280x720 PNGs, show normal menu/UI/TV
geometry and round-introduction host, instructions and effects. Each run uses
a fresh private save, muted background window and native buzzer mailbox;
no compilation, CPU tests or profiling overlap. Logs:
`analysis/vk-scene-p3-full-commands-{checked,menu,round}.{csv,stderr.log}`.
Images: `phase3/p3-full-commands-{menu,round}.{ppm,png}`. No driver DIFF/HALT/
fallback or early-exit diagnostic occurs. Source pins, original save and
private template remain unchanged. These functional checks do not establish
FPS or normalized CPU savings. Final interpreter-count, reversed same-EXE
FPS/CPU and default-selection gates remain open, along with the remaining
geometry data stages.

## Native attribute control and complete raw float3 stream (`0xAC68`, `0x93F0`)

`BUZZ_GEOMETRY_JOB_ATTRIBUTES=0/1`, default **0**, selects native attribute
orchestration and the complete built-in format-0 decoder inside the native
pipeline owner. With JOB=0 or a foreign/reference context, both entries retain
their original bodies. The generated adapter now requires routing protocol 7;
CMake rejects an older adapter. The 14-entry job table is unchanged; a separate
two-entry table supplies stream decoding and the retained output-stride getter.

`port/native_geometry_attributes.h` implements primary/secondary stream control,
input pointer publication, normal lower-input selection, special output-stride
extent calculation, full spills/restores and the final pointer setter. The raw
decoder owns allocation of the first free float4 table, position-pointer
publication, every data group including small streams, the extra pipelined
lookahead and the complete return ABI. Its host integer SIMD loop expands four
BE float3 records from 48 to 64 bytes and inserts exact 1.0 W bits. It does not
execute an original final conversion iteration. NaNs and padding are bit data.

Preflight validates the original dispatch/selectors and physical lookahead/output
ranges, excludes context/stack/input/output aliasing and makes rejection decisions
before writes. Zero vertices preserve the no-data return even with otherwise
unused invalid format/source arguments. The new flag owns this complete decoder;
the older leaf bit 8 continues to control `0x97A8` when this new entry is disabled
or retains an original path.

Descriptor decoding (`0x7DF8`) and built-in formats 1..5 remain explicit original
callees; `0x40C8` remains the original stride getter. The `attributes` counter
measures native outer control. The separate `raw-stream` counter counts retained
non-raw/unsupported paths as fallback, never as native decoder coverage. Native
unit-vector/half leaves inside retained descriptor bodies do not close their
enclosing decoder inventory.

Validation commands:

```powershell
.\tests\scripts\test-geometry-attributes.ps1
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
.\tests\scripts\test-geometry-job-runtime.ps1
.\tests\scripts\test-geometry-full-commands.ps1
```

The pinned private oracle retains 26 original blocks, including real raw-stream,
attribute-control and stride callees. Nine external/dead or unsupported boundaries
abort if reached; no handwritten simulated decoder serves as the reference.
PASS: **720 strict raw-stream cases + 704 strict attribute-control cases**, with
original and native raw callees, all 16 first-free slots, counts 0..2048, partial
groups, random upper lanes/padding and special float bit patterns. An independent
parser compares every expanded XYZ and W word. Twenty range/alias/dispatch/slot
rejections leave state unchanged. All 128 GPRs, all 256 KiB LS, return/control
state and no-I/O behavior are checked. Owner/foreign/disabled/null-table/
unsupported/post-scope routing passes, along with 1,024 outer model scenarios,
19,508 real model entries, 43 controller scenarios and the existing 8,512 strict
full-command cases after moving the shared synchronous call helper.

Frozen build: `build-vulkan-phase3-attributes`, EXE SHA256
`1c977392704388258e6af54e1c29eb7f8f47a2bb4f99554ff7f6f160523f277f`.
Private `phase3/attributes-build.json` binds code, adapter, map and saves;
`phase3/frozen-attributes-source.c` preserves the exact generated source.
Four arms on that same EXE each pass **19,508 exact jobs / 58,195 transfers**,
with zero input divergence/failure:

| Arm | Configuration and coverage |
|---|---|
| Original | JOB=0, ATTRIBUTES=1, older stage flags 0: no native owner; original behavior retained |
| Independent attributes | JOB=2, ATTRIBUTES=1, older stage flags 0: 19,508 native attribute controls and 19,508 complete raw streams, zero raw-format rejection; 11,414 retained stream calls reported separately; 19,508 strict whole-job/I/O checks |
| Combined actual native | JOB=1, all stage flags 1: same attribute/raw counts, 11,414 retained stream calls; existing command/output counts unchanged with zero fallback |
| Combined checked | JOB=2, all stage flags 1: same stage counts plus 19,508 strict whole-job/I/O checks |

Private logs: `phase3/attributes-corpus-{off,independent,native,checked}.log`.
The older `0x97A8` leaf records no native execution in the combined actual arm,
confirming the new raw data path does not merely delegate its conversion loop.
Native-owner counters do not increment for JOB=2's retained reference.
The 11,414 retained calls remain unfinished stream-decoder coverage; they are
not converted into successes by the native enclosing-control count.
New switches remain off, mask 827 is unchanged, and Stage F's accepted exception
is unchanged. Diagnostic replay time does not prove a CPU/FPS improvement.

On the same frozen EXE, the 65 s checked menu run passes **at least 450,000
strict whole-job/I/O comparisons**, including 450,000 native attribute controls
and raw streams. It also executes at least 255,000 full command emissions,
120,000 position emissions, 375,000 binding/reset/size/index/vertex stages each
and 450,000 preparation/publication calls each. Its CSV records 2,097 frames
through 64.707 s. The actual-native menu run records 3,151 frames through
74.705 s. The actual-native round starts at 136.5 s, continues for 30 s and
records 7,661 frames through 166.224 s. These are functional observations,
not matched performance measurements.

Renderer readbacks at frames 2,500 (menu) and 6,500 (round introduction) were
converted to 1280x720 PNGs and visually inspected. Menu UI, monitors and studio
geometry are normal; the round has a normal host, green instruction display,
platform and lighting. Logs:
`analysis/vk-scene-p3-attributes-{checked,menu,round}.{csv,stderr.log}`.
Images: `phase3/p3-attributes-{menu,round}.{ppm,png}`. All three runs use
`BUZZ_STAGEF=1`, `BUZZ_GEOMETRY=1`, all new stage flags 1, a fresh private save,
muted background rendering and the native buzzer mailbox. No compilation,
CPU tests or profiling overlap. There is no DIFF/HALT/mismatch/early-exit
diagnostic. Original save, private template, frozen code/EXE/map and source
bindings were verified afterwards.

This closes the format-0 decoder increment, not the attribute-codec subsystem
or phase 3. Next work is the remaining descriptor decoder and its real data
handlers, followed by the other retained pipeline stages. The complete-data-
path interpreter-count, reversed menu/round FPS and normalized CPU-cost gates
and the final default decision remain open. No new default is enabled.

## Complete descriptor U8 color codec (`0x89B0`)

The next descriptor-decoder increment implements storage 4's complete color
region `0x89B0..0x8B90`, behind **`BUZZ_GEOMETRY_JOB_CODECS=0/1`, default 0**.
`port/native_geometry_color.h` owns column/batch iteration and SIMD conversion
with the reviewed SPU RTZ FMA. Per-column final ABI materialization reproduces
every volatile vector, lookahead and scratch write; it does not execute an
original final iteration. This closes one complete typed data handler inside
`0x7DF8`; the enclosing decoder and its other handlers remain required work.

The original handler is an internal region, not a callable SPU root. Its native
hook preserves r0 and host depth, schedules the original `0x83C8` continuation
and runs only for the native pipeline owner. JOB=2's retained reference, foreign
contexts and disabled switches stay original. Null continuation or unsupported
input retains the original before mutation. Routing protocol 8 is required by
both generator and CMake. The new `color-codec` counter is separate from whole
descriptor-control coverage; the existing `raw-stream` retained-call count does
not become native success merely because an inner typed handler is native.
Output-stage diagnostics now report the 256-call milestone as well as each
5,000 calls, so live coverage of a rare codec is observable.

JOB=2 additionally clones the DMA-free codec entry and runs the original five
blocks only up to `0x83C8`. All 128 GPRs, all LS, architectural controls,
continuation, host depth and image identity must match the native region exit
before the enclosing decoder can overwrite anything. A strict difference
fails immediately; it cannot be reported as fallback. Only a completed strict
region match increments `strict-checked`, independently of the whole-job/I/O
comparison. JOB=1 keeps the native path without this comparison cost.

Preflight covers the gather constants, group/column shapes, source pointers,
lookahead, biased logical output cursors, physical aligned writes, scratch and
nonaliasing ranges. Comparisons retain the logical cursor, including its low
bits; STQD's physical alignment applies only to memory writes. Initial gathers
always use the next quad; later within-quad gathers may wrap an unused RGB W
byte. The extra final group and the order of writes to aliased scratch remain
exact. See the U8 section in [the data map](native-geometry-data.md).

Reproduce the standalone check with:

```powershell
.\tests\scripts\test-geometry-color.ps1
```

PASS: **4,544 strict full-GPR/LS/control/continuation cases**, including 4,096
normal combinations, 256 short/no-ordinary-output column cases, 64 no-data
returns with unused invalid stack, and 128 FMA edge-vector cases. Tests vary
all 16 logical cursor biases, attribute offsets, byte sizes, column counts,
within/cross-quad paths, aliased/distinct scratch destinations and random
nonpreferred lanes. Eighteen range/shape/gather/alias rejections preserve all
state. The pinned private oracle contains the five complete original blocks;
its `0x83C8` boundary is trapped, so the test checks the exact continuation
without silently running the rest of the descriptor decoder. All channel and
unexpected call stubs abort. The 1,024 outer model cases, all 19,508 real model
entries, owner/foreign/disabled/null/reject/post-scope routing, 43 controller
scenarios and the earlier raw/attribute tests also pass.

The final frozen build is `build-vulkan-phase3-color-v4`, EXE SHA256
`b22efd6b88a8686d56df52547f72406fa01e6f8179231b7ff759afe4a3e4be69`.
`phase3/color-v4-build.json` binds source, adapter, map and saves; the private
generated source is `phase3/frozen-color-v4-source.c`. Four arms each pass
**19,508 exact jobs / 58,195 transfers**, zero input divergences/failures:

| Arm | Configuration and color-codec evidence |
|---|---|
| Original | JOB=0, CODECS=1, all older stage flags 0; native-owner hook inactive |
| Independent codec | JOB=2, CODECS=1, all older stage flags 0; 109 native color regions, zero fallback, all 109 strict region comparisons and all 19,508 whole-job/I/O checks pass |
| Combined actual native | JOB=1, all stage flags 1; 109 native color regions, zero fallback; existing attribute/output/command counts unchanged |
| Combined checked | JOB=2, all stage flags 1; same native counts, all 109 strict region comparisons and all 19,508 whole-job/I/O checks pass |

Logs: `phase3/color-v4-corpus-{off,independent,native,checked}.log`. Combined
arms retain 19,508 native attribute controls/raw streams, 11,414 explicitly
retained descriptor-stream calls, 5,078 full and 2,123 position emissions,
7,201 binding/reset/size/index/vertex stages each and 19,508 preparations/
publications each. A native typed handler does not close the retained stream
or whole-pipeline inventory. Mask 827 and the accepted Stage F baseline
exception are unchanged. Replay elapsed time is diagnostic, not performance
evidence. The native codec remains opt-in pending the final FPS/CPU decision.

The 105 s checked menu run on that same frozen EXE passes **at least 5,000
strict color-region comparisons** and **975,000 strict whole-job/I/O checks**.
Native attribute control/raw streams and command preparation/publication also
reach at least 975,000 calls each. Its CSV records 2,318 frames through
104.650 s. The codec's own strict counter proves the live function-exit gate,
independently of whether later descriptor code could hide a volatile-register
difference. Both checks use actual native Stage F and geometry (`F=1`,
`GEOMETRY=1`), without a contract override or new exception.

Actual-native functional runs complete normally: menu records 3,146 frames
through 74.675 s; the round starts at 136.5 s, continues for 30 s and records
7,651 frames through 166.164 s. Renderer readbacks at frames 2,500 and 6,500
were converted to 1280x720 PNGs and visually inspected: menu UI/monitors/studio
geometry and the round-introduction host, green instruction display and lighting
look normal. These functional observations are not matched performance results.

Logs: `analysis/vk-scene-p3-color-v4-{checked,menu,round}.{csv,stderr.log}`.
Images: `phase3/p3-color-v4-{menu,round}.{ppm,png}`. Each run uses all new stage
flags 1, a fresh private save, muted background rendering and the native buzzer
mailbox. No compilation, CPU tests or profiling overlap. There is no DIFF/HALT/
mismatch/early-exit diagnostic. Frozen code/EXE/map/source bindings, original
save and private template were verified afterwards.

This increment completes the supported U8 color data region. `0x7DF8` control,
other typed handlers and the remaining pipeline bodies still require native
implementation and their own evidence. Final interpreted-SPU counts, reversed
same-EXE menu/round FPS and normalized worker/process CPU/cycles per completed
job/frame, and the default-on decision remain open. Strict Stage F stays the
mandatory final phase 7; its temporary exception is not expanded here.

## Native descriptor control, position padding and blend orchestration (`0x7DF8`)

`BUZZ_GEOMETRY_JOB_DESCRIPTORS=0/1`, default **0**, selects native descriptor
iteration, slot selection, routing/pointer updates, position padding and blend
orchestration. `port/native_geometry_descriptors.h` preserves the original
1,056-byte frame, all 48 nonvolatile vectors and complete exit ABI. Its host
attribute loop replaces the original control dispatch; its data loop fills
missing position components across the rounded vertex range, preserving the
extra 64-byte lookahead and final volatile state. A zero vertex count retains
the original no-data path without reading unused descriptor/blend inputs.

The runtime uses an explicit nine-handler table plus the retained `0xC750`
blend kernel and `0x83C8` region continuation. Typed handlers may use their
existing validated leaves; the independent CODECS switch selects the complete
native U8 handler. The descriptor-control switch does not claim that the
remaining typed handlers or blend data kernel are native. Separate
`descriptor-dispatch` counters record handler selection without labeling it
native coverage. The 14-entry outer job table is unchanged. The new descriptor
table requires routing protocol 9; CMake rejects an older adapter.

The native owner alone takes the new control route. JOB=2's retained original,
foreign contexts, disabled switches, invalid function tables and unsupported
inputs retain the original before mutation. JOB=2 also runs an isolated
DMA-free original `0x7DF8` comparison from the identical entry, checking all
GPR/LS/control/return state plus host depth and image identity before the caller
can overwrite the exit ABI. It additionally retains the whole-job/I/O check.
No new checker exception is introduced.

Preflight checks context/frame/descriptor/table ranges, supported attribute
shapes and dispatch constants, free/existing routing slots, temporary and
destination arrays and their separation from immutable metadata. Positive
blend factors require the original context flag and a valid BE-u16 byte-offset
remap. The destination geometry count can exceed the additional-stream count;
used remap offsets are checked against that destination extent. Original
typed data callbacks and the blend kernel remain explicit boundaries.

Standalone validation:

```powershell
.\tests\scripts\test-geometry-descriptors.ps1
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
.\tests\scripts\test-geometry-job-runtime.ps1
```

PASS: **4,192 strict full-state descriptor cases and 28 unchanged-state/no-I/O
rejections**. The matrix covers all nine storage families with real original
typed and blend callees, original/native U8 callees, odd/even strides, column
counts, partial/empty groups, position-only filtering, missing/repeated routing,
normal/blend paths, remap guards, random upper lanes and SPU blend-factor edge
patterns. Its reference retains 64 original blocks; the sole external assertion
handler aborts if reached. A pinned private RO-data fixture supports the original
computed dispatch and math tables. The fixture and all derived code stay under
`analysis/`; its SHA256 is checked before test generation. Owner/foreign/off/
null/unsupported/post-scope routing, 1,024 outer model cases, all 19,508 real
model entries and 43 controller scenarios pass. The prior 4,544 U8 and
720 raw + 704 attribute cases, with their rejection checks, also pass.

Frozen build: `build-vulkan-phase3-descriptors`, EXE SHA256
`d52af6775a0e059399d0085bcef7f8f5ba526a00f71ed5b8ca53db01818d089f`.
Private `phase3/descriptors-build.json` binds sources, adapter, map and saves;
`phase3/frozen-descriptors-source.c` preserves the generated source. Four
corpus arms each pass **19,508 exact jobs / 58,195 transfers**, with zero input
divergences/failures:

| Arm | Configuration and control coverage |
|---|---|
| Original | JOB=0, DESCRIPTORS=1, all older stage flags 0: native owner inactive |
| Independent descriptor control | JOB=2, DESCRIPTORS=1, all older stage flags 0: 12,309 native function calls, zero fallback, all 12,309 strict function-exit comparisons and 19,508 whole-job/I/O checks pass |
| Combined actual native | JOB=1, all stage flags 1: same control coverage, 109 native U8 regions, existing attribute/output/command counts preserved |
| Combined checked | JOB=2, all stage flags 1: same counts plus all 12,309 strict descriptor-function exits, 109 strict U8 exits and 19,508 whole-job/I/O checks |

Logs: `phase3/descriptors-corpus-{off,independent,native,checked}.log`.
Handler selections are 12 float32, 12,431 half, 109 U8, three fixed-point and
12,067 unit-vector calls; IDs 1/5/6/7 have no observed dispatches. These are
explicit callback selections, not native-data coverage. The fixed-point calls
come from additional streams and refine the earlier primary-position-only
inventory. See the updated data map.

Combined runs still report 19,508 native attribute controls/raw streams and
11,414 raw-stream fallbacks: those fallbacks mean the original `0x93F0` format
selector delegates to the now-native `0x7DF8` control. They do not indicate a
failed descriptor-control replacement, nor do they establish native typed-data
coverage. Output/command counts remain 7,201 size/index/vertex/binding/reset
stages each, 5,078 full and 2,123 position emissions and 19,508 preparations/
publications each, with zero stage fallback. Mask 827 and the Stage F baseline
exception are unchanged; replay timings do not prove CPU/FPS savings.

On that same EXE, the 105 s checked menu run passes **at least 450,000 strict
descriptor-function comparisons** and **665,000 strict whole-job/I/O checks**.
Its CSV records 2,191 frames through 104.452 s. This verifies the function exit
before callers can overwrite volatile state, together with the enclosing job
and ordered side effects. Actual native Stage F and geometry are used
(`BUZZ_STAGEF=1`, `BUZZ_GEOMETRY=1`), without an override or new exception.

Actual-native functional runs complete normally: menu records 3,129 frames
through 74.569 s; the round starts at 136.4 s, continues for 30 s and records
7,503 frames through 166.084 s. Renderer readbacks at frames 2,500 and 6,500
were converted to 1280x720 PNGs and visually inspected. Menu UI, monitors and
studio geometry are normal; the round-introduction host, green instruction
display and lighting look normal. These are functional observations, not
matched performance measurements.

Logs: `analysis/vk-scene-p3-descriptors-{checked,menu,round}.{csv,stderr.log}`.
Images: `phase3/p3-descriptors-{menu,round}.{ppm,png}`. Each run uses all new
stage flags 1, a fresh private save, muted background rendering and the native
buzzer mailbox. There is no compile/CPU-test/profiling overlap and no DIFF/HALT/
mismatch/early-exit diagnostic. Frozen code/EXE/map/source bindings, original
save and private template were verified afterwards. The old U8 region also
retains at least 256 strict live matches with the new enclosing control.

Native descriptor control and padding are now implemented across the captured
paths. Complete typed data handlers, the blend data kernel and other pipeline
bodies remain required phase-3 work. Final interpreted-SPU counts, reversed
same-EXE menu/round FPS and normalized worker/process CPU/cycles per completed
job/frame, and the default decision remain open. New switches stay default 0;
the accepted Stage F exception is unchanged, with strict cleanup last in phase 7.

## Remaining pipeline inventory

This earlier boundary inventory predates the complete typed decoders and
weighted scatter; their sections below record the subsequent implementations.
This table tracks the outer driver's library boundaries explicitly. Completing
a row still requires its supported formats/paths and normal gates; native
coverage of one leaf does not make the entire enclosing stage native.

| Library entry | Role | Native status |
|---|---|---|
| `0xAC68` | Primary/secondary attribute decode | Native opt-in control; descriptor decoder and stride getter remain explicit original callees |
| `0x93F0`, format 0 | Raw float3 stream | Native opt-in complete decoder, including allocation, small groups and exact final ABI |
| `0x7DF8` control and codec dispatch | Descriptor attribute streams | Native opt-in iteration, slot/routing/padding/blend control; typed data and blend callees tracked separately |
| `0x85E8`, `0x87CC`, `0x8378`, `0x8B94`, `0x8D34`, `0x8EA8`, `0x9058`, `0x9114` | Remaining typed descriptor data handlers | Original bodies, with validated inner leaves where available; whole handlers still required |
| `0xC750` | Weighted scatter into existing vertex tables | Original data kernel called by native blend orchestration |
| `0x89B0..0x8B90` | U8 normalized-color descriptor handler | Complete native opt-in column/batch loop and continuation; other typed handlers remain original |
| `0x93F0`, formats 1..5 | Other built-in input streams | Retained original, no corpus native-coverage claim |
| `0x0BF8` | Additional streams/blend and DMA lists | Retained library |
| `0x09F8` | Skin/rigid controller | Native opt-in controller now complete; AF18 data flow and complete B880 region remain separate work, detailed below |
| `0xAEA0` | Index decoding | Retained library; validated index/triangle kernels inside |
| `0x6608` | Transform/cull | Retained library with existing Stage F; strict cleanup remains phase 7 |
| `0x2B80` | Output buffer sizing | Native opt-in layout/size path above; unsupported families retain library |
| `0x37C0` | Ring allocation/atomics | Retained library |
| `0x2DC0` | Index publication | Native opt-in path above; other input families retain library |
| `0x54E8` | Vertex conversion/packing | Retained library; validated output kernels inside |
| `0x35F0` | Vertex publication | Native opt-in publication using shared host layout; unsupported families retain library |
| `0x12F0` | Full multipass command emission | Native opt-in emitter with host pass/state/packet planning; shared stages retain independent flags |
| `0x02E8` | Position-only command emission | Native opt-in emitter body and writers; preparation/publication retain independent flags |
| `0x0100`, `0x0020` | Attribute binding and reset packets | Native opt-in shared writers, including calls inside the retained full emitter |
| `0x32C8` | Command-buffer preparation for empty and drawn output | Native opt-in reservation, scans, fenced GET and command preamble |
| `0x27A0` | Final command relocation/publication | Native opt-in semaphore append, packet split/relocation, NOP fill and fenced PUT; unsupported families retain library |

The host input model, context initialization and complete outer control are
native. Host-buffer/data-path integration and the retained stage bodies above
remain phase-3 work; these native increments do not close the phase or enable
a new default.

## Remaining phase-3 gates

- Complete the native data path behind the outer driver using the validated
  routines, with host buffers/state between stages wherever observable
  LS/GPR/IO effects remain exact. Preserve per-kernel switches, entry
  eligibility and whole-job fallback. A renamed or merged lift alone does not
  complete this requirement.
- Repeat the complete 19,508-job / 58,195-transfer corpus checks on the
  completed data path in checked and actual-native execution, expanding
  coverage for newly supported paths.
- Record at least 256 strict live job comparisons, normal actual-native menu
  and round frames, and unchanged interpreted-SPU counts.
- Complete same-EXE reversed menu/round FPS and matched CPU-cost measurements,
  decide the default from evidence, validate the selected configuration,
  finalize this report and commit phase results before advancing to phase 4.

The active phase-3 goal remains unfinished. Subsequent native phases continue
under the user's standing authorization once these requirements are met.

## Complete storage-9 unit-vector data decoder (2026-10-31)

`port/native_geometry_unit_vectors.h` replaces the entire `0x9114` entry,
`0x9148` setup and `0x92A0` pipeline on supported streams. Host column/batch
loops decode packed coordinates with eight-lane AVX2 SPU RTZ arithmetic.
The final two groups reproduce the complete pipeline state with native math
and memory operations; the old lifted last-column callback is absent. This
completes actual storage-9 data work beyond the earlier phase-2 inner leaf
and the native descriptor controller. At that milestone, half, float32, fixed-point and the remaining typed
handlers and weighted scatter still required work; the following increment
completes half. The current inventory below records the remaining paths.

The independent `BUZZ_GEOMETRY_JOB_UNIT_VECTORS=0/1` switch defaults to 0.
It routes only the current native pipeline owner; JOB=0, disabled,
foreign/reference clones, null continuation and unsupported inputs retain
the lifted entry before writes. Existing `BUZZ_GEOMETRY_JOB_CODECS=1` still
means U8 color only. The phase-3 generator/CMake routing protocol is 10;
the fourteen outer-driver callees and all previous switches are unchanged.
New stage 12 reports unit-vector native/fallback coverage separately.

JOB=2 compares all 128 GPRs, all 256 KiB LS, architectural controls, exact
0x83C8 continuation, host depth and image identity immediately at the codec
exit, in addition to the enclosing strict whole-job/I/O comparison. No new
exception or replay-contract override is introduced. The narrowly accepted
Stage F baseline contract remains temporary until final phase 7.

A pinned three-block original oracle and private RO fixture validate 8,256
strict cases: 4,096 column/stride/bias/count/sentinel variants, 64 zero-count
entries with unused invalid pointers, 3,072 coordinate/selector combinations
and 1,024 arbitrary reserved-selector padded tails. Twenty-three rejected
inputs leave state unchanged and perform no I/O. The descriptor's 4,192-case
all-family matrix now mixes original and complete-native unit-vector callees
as well as native color; 28 unchanged-state rejects pass. Owner/off/foreign/
null/unsupported/post-scope routing, 1,024 driver models and all 19,508 real
entries pass; the 43 controller scenarios remain passing.

The first diagnostic replay was state-exact but exposed a coverage guard
that incorrectly demanded active-vector selectors in unused padded reads.
The final native ABI handles their actual table outcomes. The second probe
passes all 19,508 jobs / 58,195 transfers with 12,067 native unit-vector
regions, zero fallback, 12,067 strict region comparisons and 19,508 strict
whole-job/I/O comparisons. Final frozen replay/live evidence follows below;
these diagnostic timings are not performance measurements.

The required phase-3 data inventory after this increment is:

| Region | Native status | Remaining work |
|---|---|---|
| `0x93F0`, storage 0 raw float3 | Complete, opt-in | Final aggregate performance/default gates |
| `0x93F0`, custom descriptor dispatch | Complete native opt-in control/ABI to `0x7DF8`; strict corpus/live and native render gates pass; separate data decoder switches | Final performance/default decision |
| `0x93F0`, built-in input formats 1..5 | Original bodies; no real corpus occurrences | Native implementations/validation remain separate; custom coverage is not their coverage |
| `0x89B0..8B90`, storage 4 U8 color | Complete, opt-in | Final aggregate performance/default gates |
| `0x9114/9148/92A0`, storage 9 unit vectors | Complete data implementation, opt-in | Final aggregate performance/default gates |
| `0x85E8`, storage 1 SNORM16 | Complete native implementation; strict synthetic/routing checks pass; default-off | Real corpus/live coverage and >=256 strict live gate OPEN; final performance/default gates open |
| `0x87CC`, storage 2 float32 | Complete, opt-in | Final aggregate performance/default gates |
| `0x8378` / `0xCAC0` family, storage 3 half | Complete, opt-in | Final aggregate performance/default gates |
| `0x8B94`, storage 5 I16 | Complete native implementation; strict synthetic/routing checks pass; default-off | Real corpus/live coverage and >=256 strict live gate OPEN; final performance/default gates open |
| `0x8D34`, storage 6 CMP | Complete native implementation; strict synthetic/routing checks pass; default-off | Real corpus/live coverage and >=256 strict live gate OPEN; final performance/default gates open |
| `0x8EA8`, storage 7 U8 integer | Complete native implementation; strict synthetic/routing checks pass; default-off | Real corpus/live coverage and >=256 strict live gate OPEN; final performance/default gates open |
| `0x9058` / `0xC810/C830/C910`, storage 8 fixed point | Complete native opt-in; strict synthetic/corpus/live checks pass | Final performance/default decision |
| `0xC750/C7A8`, remapped weighted scatter | Complete native opt-in; strict synthetic/corpus/live checks pass | Final performance/default decision |
| `0x0BF8`, list/blend stage | Native opt-in reverse selection, double-buffered DMA and caller restoration; corpus/live/render checks pass; explicit library data calls | Final performance/default decision; run-list data kernel validation below |
| `0x7118/7298`, compressed run-list decoder called by `0x0BF8` | Complete native opt-in backward expansion; strict synthetic/corpus/live and native rendering checks pass | Final performance/default decision |
| `0x09F8`, skin/rigid controller | Complete native opt-in control/ABI; strict synthetic/corpus/live and native menu/round checks pass; separate weighted/rigid counters | Final performance/default decision |
| `0xAF18`, weighted skin orchestration | Native opt-in palette transpose/frame/stream selection/batching/publication; valid path has no DMA; strict tests/replay pass, live evidence below | Cold branch gates and final performance/default decision; explicit retained data libraries do not count as new payload coverage |
| `0xB880/B934/B940`, weighted position/matrix data | Complete native opt-in; original setup/final pair replaced; strict synthetic/corpus/live and native menu/round gates pass, including actual palette aliases | Final performance/default decision; one-pair API inputs observed only synthetically |
| `0xB348` data suffix / `B3C0/B3C8`, rigid position/matrix data | Complete native pair pipeline with strict synthetic and AF18 integration checks; separate switch default0 | Real-corpus and >=256 live gates OPEN (zero observed calls); final measurements/default decision |
| `0xBB58/BB68`, cofactor normal data | Complete native host group loop with strict synthetic/composed-data checks; independent switch default0 | Nonzero real/native corpus and >=256 live gates OPEN (zero observed calls); final performance/default decision |
| `0xB4C0`, skin directional dispatch | Native host dispatch within AF18, preserving cached data inputs and all nineteen table entries | Cold target gates and final performance/default decision; each data variant retains its own inventory row |
| `0xBD4C/BE20`, basic one/two-stream data | Complete native opt-in; strict synthetic/corpus/live and native menu/round checks pass | Final performance/default decision |
| `0xBF7C`, basic three-stream data | Native implementation with strict synthetic/routing checks; zero real calls | Real corpus/live gates OPEN; final performance/default decision |
| `0xC14C/C280/B5A0`, normalized direction data | Complete native one/two/three-stream implementations with strict synthetic/composed checks; own switch default0 | Nonzero real/native corpus and >=256 live gates OPEN in all variants; final performance/default decision |
| `0xAEA0`, index stage | Validated inner leaves; enclosing stage original | Native enclosing flow and validation |
| `0x6608`, transform stage | Existing Stage F inner kernels; enclosing stage original | Native enclosing flow; strict Stage F cleanup remains final phase 7 |
| `0x37C0`, output ring/atomics | Original allocator | Native allocation/I/O flow and validation |
| `0x54E8`, vertex conversion | Validated inner leaves; enclosing stage original | Complete native conversion stage and validation |

Native control and native inner leaves are distinct from complete native data
handlers in this inventory. None of the remaining rows is considered done
because its caller or a callee is already native. Original fallback remains
available for unsupported paths, and every new implementation keeps its own
switch and strict checks. Phase 3 remains open until the required path and
final interpreted-count, reversed FPS/frame-time and normalized worker/
process CPU-cycle/time gates are satisfied.

### Frozen complete-unit-vector replay and live evidence

`phase3/unit-vectors-build.json` binds the unchanged native sources, tests,
private constant fixture policy, saves and frozen
`build-vulkan-phase3-unit-vectors` EXE SHA256
`41f595d9820ce3878b18c14295e93c43c742005cc4cf2a04fce0ce84c4ab4cb1`;
map SHA256 `74f9d3e72c081e66f31f2075413268593dd9521a883ca4fc5846c9c885048d50`.
`phase3/frozen-unit-vectors-source.c` preserves the generated adapter.

All four `unit-vectors-corpus-{off,independent,native,checked}.log` arms pass
**19,508 exact jobs / 58,195 transfers**, zero concurrent input changes and
zero failures. Leaf mask 827 and Stage F mode 2 preserve the accepted narrow
baseline contract without a replay override. The independent arm uses JOB=2
and UNIT_VECTORS=1 with all older stage switches off; combined JOB=1/JOB=2
arms enable all stage switches. Every native arm covers **12,067 complete
unit-vector regions with zero fallback**. Both JOB=2 arms check all 12,067
region exits and all 19,508 whole-job/I/O transcripts. Combined descriptor
coverage stays 12,309/0 fallback, with 109/0 native U8 regions; prior command
and publication counts remain unchanged. The off arm confirms that UNIT=1
cannot route a context outside the native owner scope.

The same frozen EXE then runs Stage F=1 / GEOMETRY=1 / all stage switches=1
against fresh private save copies, muted in the background with the native
buzzer mailbox. There is no concurrent compilation, CPU test or profiling.
`p3-unit-vectors-checked` runs for 105 seconds and logs at least **350,000
strict unit-region comparisons**, 355,000 descriptor-function comparisons,
525,000 strict whole-job/I/O comparisons and at least 256 strict U8 checks.
Actual-native menu runs for 75 seconds and round for 30 seconds after a
136.5-second navigation/load. All three live arms exceed 256 native unit
calls, with no unit fallback, mismatch, halt or early exit.

Renderer frame 2,500 of the menu and frame 6,500 of the round are saved as
`phase3/p3-unit-vectors-{menu,round}.ppm/.png` and visually inspected: normal
menu cards, studio monitors/control desk, host geometry, green instruction
screen and lighting. Live CSV evidence is respectively 2139,
3141 and 7586 frames, ending at
104425.173, 74880.561 and
166218.486 ms. It establishes duration and scene
coverage, not a matched FPS/CPU benefit. Original and canonical template save
hashes remain unchanged; the verifier checks code/EXE/map/source/save/proof
bindings again after the commit.

The unit-vector switch remains default-off. Same-frozen reversed menu/round
A/B, interpreted-SPU counts and normalized worker/process CPU-cycle/time
measurements remain final phase-3 gates after the required data inventory is
implemented. A repeatable reduction in CPU cost can justify default-on even
with flat FPS; raw CPU percentage and diagnostic replay timing cannot.

## Complete storage-3 halfword attribute decoder (2026-10-31)

`port/native_geometry_half_stream.h` replaces all data and control of the
storage-3 region: `0x8378` plus `0xCAC0/CAD8/CB78`. Host column/group loops
use AVX2 pairs of four decoded components and reconstruct only the final
pipeline iteration's full ABI. No original last-column callback remains.
Ten setup-only registers retain their group-zero values. The implementation
preserves the table shuffle's low output bits, all padded preloads, biased
logical/physical output addressing, sentinel aliases and store order 1,2,0,3.
IEEE half conversion alone would violate this contract.

The independent `BUZZ_GEOMETRY_JOB_HALF_STREAM=0/1` switch defaults to 0.
Only the current native pipeline owner routes it; disabled, JOB=0,
foreign/reference, null-continuation and unsupported inputs retain the
lift before mutation. Existing U8 and unit-vector switches keep their prior
meaning and defaults. Stage 13 separately counts half-region native and
fallback executions; the adapter/CMake routing protocol is 11. The fourteen
outer-driver callees and leaf mask 827 are unchanged.

JOB=2 checks all 128 GPRs, all 256 KiB LS, controls, exact 0x83C8 continuation,
host depth and image identity at this region's exit, as well as the enclosing
strict whole-job/I/O transcript. The existing temporary Stage F baseline
exception is unchanged; the new decoder has no relaxed-state exception.

A pinned four-block private original oracle passes **8,192 full-state cases**:
4,096 column/stride/bias/count/sentinel variants and 4,096 batches covering
**all 65,536 halfword encodings**, including independently varied aligned
quad-byte contributions. Twenty-three preflight rejects leave the context
unchanged and perform no I/O. The descriptor matrix now mixes complete
native half, unit-vector and color callees with original typed/blend code;
4,192 strict all-family cases and 28 rejects pass. Driver routing additionally
checks the new region against its actual four-block original oracle in
JOB=2, alongside owner/off/foreign/null/unsupported/post-scope cases,
1,024 models and all 19,508 real driver entries. All 43 controller cases and 104 production-getter configuration cases pass.

The preliminary combined JOB=2 replay passes all 19,508 jobs / 58,195
transfers with **12,431 complete native half regions / zero fallback**,
12,431 strict half-region comparisons and all 19,508 strict whole-job/I/O
comparisons. Native unit-vector coverage remains 12,067/0, descriptor control
12,309/0 and U8 color 109/0. Final frozen evidence follows below; diagnostic
replay timings do not establish a performance benefit.

### Frozen half-stream replay and live evidence

`phase3/half-stream-build.json` binds the native sources, tests, private
fixture policy, saves and frozen `build-vulkan-phase3-half-stream` EXE SHA256
`ffb19a32f74a043587aa3e637c5081c6c4bc7b6fda73b70e6d7a1359ba64eb14`;
map SHA256 `9b462f04af66a62a8b4402c950bc80b1f2ddbea8258f8d946e49e5af1ad60bd0`.
`phase3/frozen-half-stream-source.c` preserves the generated adapter.

All four `half-stream-corpus-{off,independent,native,checked}.log` arms pass
**19,508 exact jobs / 58,195 transfers**, zero concurrent input changes and
zero failures, using leaf mask 827 / Stage F mode 2 without a replay override.
The independent arm uses JOB=2 / HALF_STREAM=1 with older stage flags off;
combined JOB=1/JOB=2 arms enable all stage flags. Every native arm covers
**12,431 complete half regions with zero fallback**. JOB=2 checks all 12,431
region exits and 19,508 whole-job/I/O transcripts. Combined unit-vector,
descriptor and U8 coverage stays 12,067/0, 12,309/0 and 109/0 respectively;
previous output/command counts are unchanged. JOB=0 with HALF_STREAM=1
confirms there is no routing outside the current native owner scope.

The same frozen EXE then uses Stage F=1 / GEOMETRY=1 / all stage switches=1
for fresh private saves, background/muted execution and native buzzer input.
No compilation, CPU test or profiling overlaps these runs. The 105-second
checked menu logs at least **280,000 strict half-region comparisons**,
285,000 unit-region and descriptor-function comparisons, **425,000 strict
whole-job/I/O comparisons** and 256 strict U8 comparisons. Actual-native menu
runs 75 seconds; round runs 30 seconds after 136.5 seconds of navigation/load.
All three arms exceed 256 actual-native half and unit calls, without half
fallback, mismatch, halt or early exit.

Renderer frame 2,500 of the menu and 6,500 of the round are saved in
`phase3/p3-half-stream-{menu,round}.ppm/.png` and visually inspected:
normal menu cards, studio monitors/control desk, crowd geometry and green
illuminated stairs. CSVs contain 2097, 3117
and 7503 frames, ending at 104620.911,
74933.432 and 166495.461 ms. These establish
functional duration/scene coverage; they are not matched FPS/CPU results.
Original and canonical template save hashes stay unchanged. Code, EXE, map,
source, fixture, save and archived proof-log hashes are verified again after
the commit.

HALF_STREAM remains default-off. Float32, fixed-point, the other required
typed handlers, weighted scatter and the enclosing stages in the inventory
remain open. Interpreted-SPU counts, same-frozen reversed menu/round A/B and
normalized worker/process cycles and CPU time per completed frame/job are
still required before the final phase-3 default decision. Repeatable CPU
savings can justify enabling a correct routine even with flat FPS.

## Complete storage-2 float32 attribute decoder (2026-10-31)

`port/native_geometry_float32_stream.h` replaces the full `0x87CC` entry,
`0x8800` setup, both `0x8878/8928` loops and `0x8910` continuation control.
Host byte-vector loops preserve raw float payloads without arithmetic;
only each column's final iteration materializes the pipeline ABI. No lifted
setup, loop or final-column callback remains. Within-quad rotation and
cross-quad copying stay distinct, including unused components, initial
following-quads and disjoint volatile registers retained from earlier paths.
All clipped rows use the sentinel's preferred word, rather than separate
per-row sentinel lanes. Full-register tests deliberately vary unused sentinel
lanes to prove that distinction.

`BUZZ_GEOMETRY_JOB_FLOAT32_STREAM=0/1/2` is independent and defaults to 0:
lift, native or strict region comparison. Its mode 2 performs the same full
state comparison with native JOB=1 as with JOB=2.
Routing stays owner-scoped; disabled, JOB=0, foreign/reference, null and
unsupported paths retain the lift before mutation. Adapter/CMake protocol
is 12; stage 14 separately records float32 coverage, and rare-path logs also
show its first native/strict call. JOB=2 compares all 128 GPRs, all 256 KiB LS,
controls, exact 0x83C8 continuation, host depth and image identity immediately
at the region exit, in addition to the enclosing whole-job/I/O transcript.
The region is DMA-free, so its clone never repeats guest I/O. There is no
relaxed-state exception for this decoder. Stage F's temporary
baseline contract, leaf mask 827 and all existing switch defaults are unchanged.

A pinned five-block private original oracle passes **5,440 strict cases**:
4,096 shape/stride/width/count/bias variants, 256 partial-count mixed-path
cases, 1,024 raw float-bit cases and 64 zero-count entries with unused invalid
pointers. Twenty-five rejected inputs leave the context unchanged and perform
no I/O. The 4,192-case descriptor matrix / 28 rejects mixes original and
native float32/half/unit/color callees. Driver routing checks both JOB=2 /
FLOAT32=1 and JOB=1 / FLOAT32=2 against the actual five-block float32 oracle,
with owner/off/foreign/null/unsupported/
post-scope cases, 1,024 models and all 19,508 real driver entries. All 43
controller cases and 104 production-getter configuration cases pass.

The preliminary combined JOB=2 replay passes all 19,508 jobs / 58,195
transfers, including **12 complete native float32 regions / zero fallback**
and 12 strict float32 comparisons. Existing half/unit/descriptor/U8 coverage
stays 12,431/0, 12,067/0, 12,309/0 and 109/0. These twelve corpus calls alone
do not establish the required 256 live calls; frozen/live evidence is tracked
below, and the new default remains off.

### Provisional float32 runs retained for audit

V1 and V2 replay/live logs remain private evidence, distinct from the final
V3 build below. The first combined replay/live PowerShell process retained
an empty REPLAY_CORPUS variable and exited before rendering; final scene
runs use a fresh process and explicitly remove the variable. Full JOB=2
round validation did not reach storage 2 during bounded navigation. The
independent FLOAT32=2 mode solves this with the same strict fields and real
round invocations, without counting repeated checker clones as live calls.

V2 passed its valid-input proofs, but review found the existing publication
option's validation predicate had accidentally widened while adding float32
mode 2. It is restored to its original 0/1 contract. All thirteen production
getters now have 104 configuration checks, including invalid values; only
FLOAT32 accepts 2. V2 is not the final source-bound proof. Its frame-6,500
readback also showed profile presentation, so actual round rendering is
verified through periodic 500-frame readbacks instead.

### Final float32 V3 source-bound evidence

The final build is `build-vulkan-phase3-float32-stream-v3`, bound by
`phase3/float32-stream-v3-build.json` and preserved generated source
`phase3/frozen-float32-stream-v3-source.c`. EXE SHA256 is
`82ef9094ad955e7db3e36983d5e165b977d85523ad6fc72f100a1bcf67d3eb9f`; map SHA256
`c3e089fd6902d1bb420ff519b802d3001b2315ffc0f330eb7a1c2b3a6f21d0ae`.

All five final replay arms (off, independent, native, checked and
kernel-checked) pass **19,508 exact jobs / 58,195 transfers each**, with zero
concurrent input changes and zero failures. They use mask 827 / Stage F=2,
without a replay-contract override. All four native arms execute **12 complete
float32 regions / zero fallback**. JOB=2 compares twelve strict float32 exits
and all 19,508 whole-job/I/O transcripts; JOB=1 / FLOAT32=2 independently
compares all twelve float32 exits. Combined half/unit/descriptor/U8 native
counts remain 12,431/12,067/12,309/109, all with zero fallback. The initial
independent replay process was interrupted; its partial log is retained,
and the completed proof is `float32-stream-v3-corpus-independent-resumed.log`.

The same final EXE runs Stage F=1 / GEOMETRY=1 / older native flags=1 against
fresh private saves, muted/background with native buzzer input. No builds,
CPU tests or profiling overlap these functional runs:

| Final live arm | Mode | Verified coverage |
|---|---|---|
| `p3-float32-v3-checked`, menu 45 s | JOB=2 / FLOAT32=1 | **>=70,000 strict whole-job/I/O**; >=50,000 half, >=50,000 unit and >=50,000 descriptor comparisons; float32 absent |
| `p3-float32-v3-menu-render`, 75 s | JOB=1 / FLOAT32=1 | Actual-native menu, periodic 500-frame renderer readbacks; float32 absent |
| `p3-float32-v3-kernel-checked-round`, 60 s after 136.4 s navigation/load | JOB=1 / FLOAT32=2 | **>=20,000 strict float32 region comparisons** and >=20,000 native calls; identical full-state fields, no whole-job pairing in this arm |
| `p3-float32-v3-round-render`, 45 s after 141.8 s navigation/load | JOB=1 / FLOAT32=1 | **>=10,000 actual-native float32 calls**; periodic 500-frame renderer readbacks show the actual round |

The CSVs contain 1280, 2315, 6112, 5795 frames respectively, ending at
44326.826, 74597.514, 195876.049, 186235.555 ms. These are functional
duration/scene checks, not matched performance results.

There are no differences, halts, float32 fallbacks or early exits. The
256-call float32 gate uses actual round-sequence invocations independently
of the menu's whole-job gate. The first V3 menu run ended at frame 2,257,
before its scheduled frame-2,500 dump; it is retained and not counted as
render evidence. The final fresh menu run uses periodic readbacks instead.

`phase3/p3-float32-v3-menu-render.ppm/.png` and
`phase3/p3-float32-v3-round-render.ppm/.png` are visually inspected: normal
menu cards/studio control desk, and the actual green round stage with host,
player and score screen. Original and canonical template save hashes remain
unchanged. EXE, map, generated source, code, tests, fixture, saves and 150
archived proof logs are verified and bound to the source commit.

FLOAT32 remains default-off. Phase 3 stays open for fixed-point/other typed
handlers, weighted scatter and enclosing stages. Final interpreted-SPU counts,
reversed same-frozen menu/round A/B and normalized worker/process cycles and
CPU time per completed frame/job still gate completion and defaults.
Repeatable CPU savings can justify enabling a correct routine even when FPS
stays flat. Strict Stage F reconstruction remains mandatory final phase 7.

## Complete storage-8 fixed-point decoder (2026-10-31)

`port/native_geometry_fixed_stream.h` replaces the full `0x9058` entry and
`0xC810/C830/C910` pipeline. Host bit extraction decodes all four MSB-first
components, including zero/full-word widths; offset addition wraps before
signed integer/fraction separation and strict CSFLT/CUFLT/FA conversion.
All columns and groups are native. Only the final group reconstructs the
full ABI, including pending input, the trailing store, r107+1 columns,
0x9110 link, SP+0xD0 offset-record advancement and row store order 2,3,1,0.
No lifted final-column callback remains. The independent
`BUZZ_GEOMETRY_JOB_FIXED_STREAM=0/1/2` defaults off; mode 2 compares the
same complete region state with JOB=1 or JOB=2. Adapter protocol is 13.

The pinned four-block oracle passes **9,216 strict cases**: 4,096 shape/
stride/column/count/bias cases, 4,096 variable integer/fraction width cases,
and 1,024 rotated-metadata/offset-boundary/raw-payload cases. Thirty-three
rejections leave all context bytes unchanged and perform no I/O. The mixed
4,192-case descriptor matrix plus 28 rejects includes the complete native
fixed handler alongside original and native float32/half/unit/color paths.
Driver routing checks both whole-job checked and independent region-checked
modes against the actual four-block fixed oracle, owner/off/foreign/null/
unsupported/post-scope behavior, 1,024 models and 19,508 real driver entries.
All 43 controller cases and **112 production-getter configuration cases**
pass; only float32 and fixed-stream options accept region mode 2.

### Fixed-point frozen corpus and scene evidence

`phase3/fixed-stream-build.json` binds the final
`build-vulkan-phase3-fixed-stream` executable, map, preserved generated C,
code/tests/docs, fixtures and original/template saves. EXE SHA256:
`eb9c534bd3d0d634a4ee8a5b6cac5fd423db0ec344fb048b2fe7616b0aa081fb`; map SHA256:
`19065e43515133fd3d0d42e1dd0a2a4c18697bc2b5ab8334602f46d4d38cb49d`.

All five `fixed-stream-corpus-{off,independent,native,checked,kernel-checked}.log`
arms pass **19,508 exact jobs / 58,195 transfers each**, zero concurrent input
changes and zero failures, with mask 827 / Stage F=2 and no contract override.
All four native arms execute **3 complete fixed regions / zero fallback**.
JOB=2 arms perform three full-state fixed comparisons and 19,508 whole-job/I/O
checks. JOB=1 / FIXED_STREAM=2 independently compares those three fixed exits.
Combined float32/half/unit/descriptor/U8 coverage stays 12/12,431/12,067/12,309/
109, all with zero fallback; earlier command/output counts remain unchanged.

The same EXE uses Stage F=1 / GEOMETRY=1 / older native flags=1, fresh private
saves, muted/background rendering and native buzzer input. No builds, CPU
tests or profiling overlap these functional runs:

| Scene proof | Mode | Frames / last renderer time | Fixed native / strict lower bounds |
|---|---|---|---|
| `p3-fixed-checked` | JOB=2 / FIXED=1 | 1,994 / 44775.097 ms | 0 / 0 |
| `p3-fixed-menu` | JOB=1 / FIXED=1 | 3,202 / 74981.577 ms | 0 / 0 |
| `p3-fixed-round-render` | JOB=1 / FIXED=1 | 8,568 / 180911.291 ms | 256 / 0 |
| `p3-fixed-kernel-checked-round` | JOB=1 / FIXED=2 | 9,428 / 196042.687 ms | 5,000 / 5,000 |

The checked menu performs at least 115,000 complete whole-job/I/O
comparisons without differences. Renderer readbacks every 500 frames are
saved as `phase3/p3-fixed-menu.ppm/.png` and
`phase3/p3-fixed-round-render.ppm/.png`. Both are visually inspected: normal
menu/studio/crowd and an actual green-stage question with host, player and
answer cards. There are no differences, halts, fixed fallbacks or early exits.
The source commit also binds 158 archived controller/configuration/codec/
descriptor/driver proof logs. Original and template saves remain unchanged.

**Fixed-specific live >=256 gate: PASSED, >=5,000 strict round invocations.**
The three real corpus calls and 9,216 synthetic cases do not substitute for
live invocations. Fixed calls appear in the later round sequence, after
being absent from the menu. The native round arm also passes 256 actual fixed invocations.
Repeated reference clones are not counted as live coverage. The full-state
comparison fields are identical in JOB=1 / FIXED=2 and JOB=2. The fixed
implementation remains opt-in with default 0 until the final performance/
default decision; phase 3 remains open.

These functional CSVs are not matched FPS/CPU measurements. Final reversed
menu/round A/B, interpreted-SPU counts, normalized worker/process cycles and
CPU time per completed frame/job remain required for phase-3 defaults;
repeatable CPU savings can matter with flat FPS. Remaining typed handlers,
weighted scatter and enclosing stages remain open; strict Stage F is last.

## Complete remaining typed data implementations (2026-10-31)

Storage 1 SNORM16 (`0x85E8/8620/8698/8728/8740`), storage 5 I16
(`0x8B94/8BC8/8C30/8CAC/8CC0`), storage 6 CMP (`0x8D34/8D68/8DE0`) and
storage 7 U8 integer (`0x8EA8/8EE0/8F48/8FCC/8FE0`) now own their complete
payload loops. A shared guarded layout reader validates their LS shape,
gather tables, steps, source/lookahead/output/sentinel ranges and aliases
before mutation. Each independent switch defaults to 0 and accepts
0 lift / 1 native / 2 strict region, including strict checks with JOB=1.
The new switches are `BUZZ_GEOMETRY_JOB_SNORM_STREAM`, `_I16_STREAM`,
`_CMP_STREAM` and `_U8_INTEGER_STREAM`. Counters are stages 16..19; the
adapter protocol is 14. Existing defaults, leaf mask 827 and the limited
Stage F baseline exception remain unchanged.

SNORM16 preserves its cross-quad initial gather even in the within-quad path;
subsequent within-quad groups wrap unused components. I16 and U8 integer
wrap every within-quad group. The two-stage table shuffle zeros the low
halfword, rather than filling it with ones. SNORM16 performs CSFLT 139 and
FMA with r93/r92; I16 uses CSFLT 139; U8 integer uses CUFLT 139. CMP gathers
its packed word across quads, applies the full r90/r100 shift/mask vectors,
CSFLT 124 and r99/r98 FMA. Arbitrary parameter bits retain SPU behavior.
All families preserve per-column final ABI, branch-specific surviving
registers, trailing stores/prefetches, row order, per-lane sentinel clipping
and r121=0 immediate continuation. No lifted final-column callback remains.

| Handler | Strict full-state cases | Additional coverage |
|---|---:|---|
| SNORM16 | 8,512 | All 65,536 input encodings, 4,096 shapes, 256 partial/mixed-path layouts, 64 empty entries, arbitrary FMA parameters |
| I16 | 8,512 | All 65,536 input encodings, cross/within gather and unused components |
| U8 integer | 4,672 | All 256 input encodings, cross/within byte gather |
| CMP | 8,512 | Full 10/11-bit component ranges, arbitrary shift/mask/scale/bias vectors and packed input patterns |

Each handler has 27 unchanged-context/no-I/O rejection cases. Its pinned
private oracle executes all original region blocks and traps 0x83C8.
The 4,192-case descriptor matrix / 28 rejects mixes original and native
callbacks for all nine storage families. Driver routing uses each actual
oracle in both JOB=2/new-switch=1 and JOB=1/new-switch=2, including owner,
off/foreign/null/unsupported/post-scope guards, 1,024 models and all 19,508
real driver entries. All 43 controller and 144 production-getter cases pass.

### Remaining typed families: frozen compatibility and live coverage

`phase3/typed-streams-build.json` binds `build-vulkan-phase3-typed-streams`,
preserved `phase3/frozen-typed-streams-source.c`, code/tests/docs, fixtures,
saves and proof logs to the source commit. EXE SHA256:
`f0a782ef26f1c005bd7bc91ce40b949b052f69a351b104cdd14c7627cc719861`; map SHA256:
`bddb57117e96b3f6432f3283a29f3251125adce04f4403edc30e3f353d015139`.

All five `typed-streams-corpus-{off,independent,native,checked,kernel-checked}.log`
arms pass **19,508 exact jobs / 58,195 transfers each**, zero concurrent input
changes and zero failures, mask 827 / Stage F=2, with no contract override.
The corpus contains **zero storage-1/5/6/7 dispatches**: this proves replay
compatibility, not native-data coverage for these four handlers. Their native,
strict and fallback counters all remain zero. JOB=2 arms compare 19,508 full
whole-job/I/O transcripts. Combined older float32/fixed/half/unit/descriptor/
U8-color coverage remains 12/3/12,431/12,067/12,309/109, all without fallback.

The same EXE runs checked menu 45 s, native menu 75 s, native round 60 s
and independently region-checked round 90 s after navigation/load. All use
Stage F=1 / GEOMETRY=1 / older native switches=1, fresh private saves,
muted/background rendering and native buzzer input. All four new switches
are 1 except the final round arm, where they are 2. No compilation, CPU tests
or profiling overlap these functional runs. There are no differences, halts,
new-region fallbacks or early exits.

| Scene proof | Whole-job mode | Frames / last renderer time |
|---|---|---|
| `p3-typed-checked` | JOB=2 | 1,988 / 44441.730 ms |
| `p3-typed-menu` | JOB=1 | 3,332 / 74657.129 ms |
| `p3-typed-round-render` | JOB=1 | 9,375 / 196081.918 ms |
| `p3-typed-kernel-checked-round` | JOB=1 | 10,750 / 226100.151 ms |

The checked menu records **>=110,000 strict whole-job/I/O
comparisons**. Periodic 500-frame renderer readbacks are preserved in
`phase3/p3-typed-menu.ppm/.png` and
`phase3/p3-typed-round-render.ppm/.png`. Both are visually inspected: normal
menu/studio/crowd, and an actual green round stage with host, player and
score display. Original/template saves remain unchanged. The committed
source binds 193 archived controller/configuration/codec/descriptor/driver
proof logs.

| Handler | Actual native live lower bound | Strict live lower bound | >=256 strict live gate |
|---|---:|---:|---|
| SNORM16 | 0 | 0 | OPEN |
| I16 | 0 | 0 | OPEN |
| CMP | 0 | 0 | OPEN |
| U8 integer | 0 | 0 | OPEN |

The observed scene sequence does not establish the required live gate for
SNORM16, I16, CMP, U8 integer. These remain explicit open validation items.
Zero calls, synthetic cases and repeated checker clones are not native live
successes. A real workload exercising each missing family is still required.

All four switches remain default-off. These complete opt-in implementations
do not close phase 3 or justify promoting defaults. Weighted scatter and
the enclosing pipeline handlers remain original. Final interpreted-SPU counts,
reversed same-frozen menu/round A/B, normalized worker/process cycles and CPU
time per completed frame/job are still required. Repeatable CPU savings can
justify a routine with flat FPS. Strict Stage F reconstruction stays last.

## Complete remapped weighted scatter (2026-10-31)

`port/native_geometry_blend_scatter.h` replaces both `0xC750/C7A8` blocks.
A guarded host plan validates the negative pair cursor, complete source and
remap lookahead, physical wrapped destinations and immutable remap quads
before any mutation. The native loop carries pending masked sources,
prefetched destinations and SPU-exact FMA results in host values. Duplicate
destinations, within-pair and cross-pair hazards, biased pointers and
source/output overlap retain the original load/store order. The complete
last-iteration register state is materialized without an original callback.

`BUZZ_GEOMETRY_JOB_BLEND_SCATTER=0/1/2` defaults off: lift, native, strict
root comparison. Full-state checking works both with JOB=2 and independently
with JOB=1. All 128 GPRs, 256 KiB LS, controls, root r0 return, trampoline,
host depth and image are checked before the caller can overwrite state.
No state exception is introduced. Adapter protocol is 15; stage 20 records
scatter independently (21 counters total). The older defaults and leaf mask
827 remain unchanged, including the limited existing Stage F contract.

The pinned two-block oracle passes **6,153 strict cases**: 4,096 pair-count/
pointer-bias/duplicate-index layouts, 1,024 raw FMA mask/weight cases, 1,024
in-place source/output cases, two independent numerical hazard examples,
one unused-tail remap case, one shared read-only map/source case and four
large-count cases through the 4,096-pair limit, plus a speculative
destination read below the data area. Both root return depths and
the r0=0xC7A8 tail-PC collision are covered. Fourteen unsupported inputs leave
the complete context unchanged and perform no I/O.

The mixed descriptor matrix passes 4,192 full-state cases / 28 rejects,
including **1,198 actual native scatter calls** alongside original and native
typed handlers. Driver routing checks both JOB=2/BLEND=1 and JOB=1/BLEND=2
against the actual two-block oracle, owner/off/foreign/null-tail/unsupported/
post-scope guards, 1,024 driver models and 19,508 real driver entries. All
43 controller and 152 production-getter configuration cases pass.

### Weighted scatter: final frozen V2 evidence

`build-vulkan-phase3-blend-scatter-v2` is bound by
`phase3/blend-scatter-v2-build.json`, preserved generated C, source/tests/docs,
saves and archived proofs. EXE SHA256:
`b4181ec85ccfbf07ff10aacc0d4760a821b62bb1b6326a3307b9b79e11c46460`; map SHA256:
`505db512d712816d075039bbc02d24ba414611680595ecc14f347a719e2b02c1`.

All five `blend-scatter-v2-corpus-{off,independent,native,checked,kernel-checked}.log`
arms pass **19,508 exact jobs / 58,195 transfers each**, zero concurrent input
changes and zero failures, mask 827 / Stage F=2, with no contract override.
All four native arms execute **2,326 complete scatter roots / zero fallback**.
JOB=2 arms compare 2,326 full-state scatter exits plus all 19,508 whole-job/I/O
transcripts. JOB=1 / BLEND=2 independently checks all 2,326 root exits.
Combined older float32/fixed/half/unit/descriptor/U8-color counts remain
12/3/12,431/12,067/12,309/109, all with zero fallback. The four cold typed
families still have zero captured dispatches; no new coverage is attributed
to them.

The prototype V1 also replayed exactly, with 2,114 native calls and 212
eligibility fallbacks. Its guard incorrectly applied the data-area minimum
to the final read-only destination prefetch. V2 permits that speculative
read anywhere in physical LS while preserving actual-store guards. The
comparison fields are unchanged. The old EXE, logs and exact header/test
SHA bindings remain preserved, with the former rejection now a positive
strict test. V1 evidence is not substituted for this final V2 build.

The same final EXE runs Stage F=1 / GEOMETRY=1 / all older native flags=1,
fresh private saves, muted/background rendering and native buzzer input.
BLEND=1 is used except in the independent root-checked round (BLEND=2).
No compilation, CPU tests or profiling overlap these functional runs:

| Scene proof | Mode | Frames / last renderer time | Scatter native / strict lower bounds |
|---|---|---|---|
| `p3-blend-v2-checked` | JOB=2 / BLEND=1 | 1,291 / 44555.023 ms | 0 / 0 |
| `p3-blend-v2-menu` | JOB=1 / BLEND=1 | 2,292 / 74796.936 ms | 0 / 0 |
| `p3-blend-v2-round-render` | JOB=1 / BLEND=1 | 6,086 / 196191.689 ms | 1,300,000 / 0 |
| `p3-blend-v2-kernel-checked-round` | JOB=1 / BLEND=2 | 5,390 / 225609.268 ms | 940,000 / 940,000 |

The actual scatter live gate passes **>=940,000 strict root comparisons**;
menu whole-job checking passes **>=75,000 full-state/I/O
comparisons**. Checker clones are not counted as extra live invocations.
There are no differences, halts, scatter fallbacks or early exits.

Renderer readbacks every 500 frames are preserved in
`phase3/p3-blend-v2-menu.ppm/.png` and
`phase3/p3-blend-v2-round-render.ppm/.png`. Both are visually inspected:
normal menu/studio/crowd and the actual round stage with host, player and
score display. Original and canonical template saves remain unchanged.
Source, EXE/map/generated C, fixtures, corpus, live CSV/logs, images and 198
archived controller/configuration/codec/descriptor/driver logs are bound to
the source commit and verified again after committing.

BLEND_SCATTER remains default-off. These are correctness/render observations,
not matched FPS or CPU results. The four cold typed-family real-data/live
gates remain explicit and unchanged; no state-contract exception is added.
Enclosing list/blend, skinning, indices, transform, ring/atomics and full
vertex-conversion bodies still require native implementations. Final
interpreted-SPU counts, reversed same-frozen menu/round A/B and normalized
worker/process cycles and CPU time per completed frame/job still gate phase
3 and default selection. CPU savings can justify a routine with flat FPS.

## Strict replay checkpoints for I/O roots (2026-10-31)

`0x0BF8` is the next enclosing stage. It fetches weights/list descriptors,
double-buffers GETL payloads, filters streams, invokes `0x7118` and the typed
descriptor controller, and optionally normalizes the result. The audit adds
`0x7118` explicitly to the remaining inventory: replacing its caller alone
will not make the compressed-list data implementation native.

The controller now provides `buzz_geometry_job_io_root_begin/end` for strict
root checks inside JOB=2's native shadow. Begin clones the actual root input
and captures the parent transcript position and entry trampoline. End runs
the original root on that clone against a bounded, already-consumed fragment
of the transcript. The reference receives the same recorded GETs and channel
results and must reproduce list inputs, output payloads and operation order;
it cannot consume the caller's following operations or publish another guest
write. Parent replay position, shadow ownership and native return trampoline
are restored after the check, including for nested checkpoints.

The comparison covers all 128 GPRs, all 256 KiB LS, architectural controls,
exact return PC/trampoline, image, host depth, policy mode and resident overlay.
There are no new state exclusions. A difference is fatal even if the caller
would overwrite it before the final whole-job comparison. The API returns no
ticket outside a replay shadow; that is not a strict pass or native coverage.
Each integration must provide a synchronous original runner that stops at
its own exact return boundary and uses the audited channel hooks.

Validation:

```powershell
.\tests\scripts\test-geometry-io-root.ps1
.\tests\scripts\test-geometry-job-io.ps1
.\tests\scripts\test-geometry-job-runtime.ps1
```

PASS: **23 controller cases**: ordinary, empty-I/O and nested roots in JOB
0/1/2; rejection of GPR, LS, control, PC, image, depth, policy, overlay and
continuation differences, missing/extra operations, changed DMA-list input,
foreign owner and missing reference runner. Real channel calls occur once,
and the caller resumes on the untouched parent transcript. The I/O unit test
also checks slice bounds, sticky failures, empty slices and unchanged parent
cursor. Existing **43 controller scenarios and 152 configuration cases** pass.
Private logs are `phase3/io-root-{controller-test,regression-io,
regression-runtime}.log` under `analysis/native-geometry`.
The private `phase3/io-root-v1-proof/manifest.json` binds the tested sources,
archived logs and original lift pin; it is an infrastructure proof, not a
frozen game-build or live/native coverage manifest.

At that infrastructure checkpoint there was no BF8 native implementation,
switch or corpus/live coverage. JOB default 0, leaf mask 827, stage defaults,
adapter protocol 15 and the existing Stage F exception were unchanged.
The subsequent BF8 implementation is tracked below.
Strict Stage F reconstruction remains mandatory final phase 7.

## Native additional-stream list/blend flow BF8..12E8 (2026-10-31)

`port/native_geometry_blend_lists.h` owns reverse stream selection,
double-buffered GETL scheduling, capacity deferral, buffer resets, optional
reset-count updates, stream argument construction and caller restoration.
It uses a host loop over selected streams; no lifted BF8 control block runs
on the native path. All 128-bit ABI lanes and stack spills remain observable
and exact, including at calls to the four explicit library data operations:
`0x7118` compressed lists, `0x7DF8` typed attributes, `0x2780` normal pointer
and `0x2C00` normalization. Their own native/original implementations and
switches remain separate. At this BF8 milestone, `0x7118` was original data
work; the subsequent run-list implementation is described below.

`BUZZ_GEOMETRY_JOB_BLEND_LISTS=0/1` selects original/native, default **0**.
Ownership remains scoped to the native image-41 pipeline; original reference,
foreign and disabled calls retain the lift. Supported entries use a physical
LS, bounded aligned stack and at most 128 streams; nonempty scratch buffers
must be aligned, valid and disjoint from manager/stack. Eligibility precedes
register changes, spills and channel operations. Unsupported entries retain
the original stage, without being counted as native success.

Under JOB=2, the I/O-root checker clones the actual BF8 input and re-executes
the complete original root against its bounded parent transcript slice.
It compares all GPRs, all LS, controls, image/depth and exact return PC/FN
before the native caller resumes. The reference drains by `0xD04` tail
identity, so a return link equal to that interior PC is still checked.
JOB=1 executes the native root without replaying its I/O. This flag has no
independent mode 2: strict I/O-root checking requires the JOB=2 transcript.

Adapter protocol **16** routes BF8 with its five boundary callbacks (four
data operations and the original return tail). The adapter also registers
six exact original lexical suffixes reached by interrupt-thunk `r2+8`
branches: `0xDE8/EE8/F48/111C/1240/12C0`. This preserves the original reference
instructions while making those computed entries available to compiled
lookup. Neither the original lift nor the phase-2 generator pin changes.

Validation:

```powershell
.\tests\scripts\test-geometry-blend-lists.ps1
.\tests\scripts\test-geometry-job-runtime.ps1
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
.\tests\scripts\test-geometry-io-root.ps1
```

PASS: **2,672 strict root/call-ABI/I/O cases and 15 unchanged/no-I/O rejects**.
The pinned oracle retains 39 original blocks plus six original interior
suffixes, with four bounded test callees. Tests check all GPRs, caller stack,
PC/depth/image and architectural controls at every callee entry; the callees
spill all high registers and clobber volatile registers. Root exits compare
full GPR/LS/control/continuation state. Coverage includes 0..128 streams,
both interrupt states and host depths, return-link/tail collision, null and
present reset counters, normal-pointer variants, zero/low-weight filtering,
capacity deferral and 512 raw-FP weight/scale cases around the threshold,
denormals, signed zero and SPU exceptional encodings. There are **33,307 GETLs,
5,415 resets and 579 fully filtered nonempty cases** in this synthetic suite.
These are synthetic controller tests, not real native data-callee coverage.

PASS: **160 production-getter configuration cases**, 43 existing controller
scenarios and 23 I/O-root cases. Driver tests check owner/disabled/foreign/
null-library/unsupported BF8 routing alongside existing native routes,
1,024 pipeline models and all 19,508 real driver entries.

Frozen candidate: `build-vulkan-phase3-blend-lists-v1`.
EXE SHA-256: `8e6a41c59a38c43c15643e8f2b8cc55a8240e1bd1a397e4664adc57291fb58d6`.
Map SHA-256: `d8617bfdcc1f3c9269f0a847a399224b3c342c5676754beec4f5cddd72abc162`.
The private frozen generated source is `phase3/frozen-blend-lists-v1-source.c`.

All four frozen corpus configurations pass **19,508 exact jobs / 58,195
transfers**, zero changed-input jobs and zero failures, with Stage F=2,
geometry leaf mask 827 and no replay-contract override:

| Configuration | JOB | Other new stage flags | BF8 native / fallback | Strict BF8 roots |
|---|---|---|---|---|
| Off | 0 | 0 | No native pipeline | 0 |
| Independent | 2 | 0 | 328 / 0 | 328 |
| Combined native | 1 | 1 | 328 / 0 | 0 |
| Combined checked | 2 | 1 | 328 / 0 | 328 |

BLEND_LISTS is 1 in all four configurations, proving it cannot activate the
native stage through an original JOB=0 caller. Both JOB=2 configurations also
check the complete state and I/O of all 19,508 outer jobs. Combined native
codecs retain all previous real coverage, including 2,326 scatter calls,
12 float32 and three fixed-point calls without fallback. Cold typed families
still have zero real calls; they are not counted as validated live coverage.
Private logs: `phase3/blend-lists-v1-corpus-{off,independent,native,checked}.log`.

Native menu/round functional checks use the same frozen EXE, all new stage
flags=1, leaf mask 827, GEOMETRY=1, Stage F=1 and fresh private save copies:

- JOB=1 menu, 75 seconds: 2,231 CSV frames through 74,687.511 ms. The native
  renderer readback shows the normal studio/menu UI, crowd and stage.
- JOB=1 round, 60 seconds after `round_start_s=136.5`: 6,023 CSV frames through
  195,875.684 ms and at least **140,000 native BF8 roots**. The final renderer
  readback shows the actual round's green stage, host, player and scoreboard.

- The first combined JOB=2 fixed-timing diagnostic records 2,534 frames
  through 236,427.243 ms but its readback is a profile prompt. It performs
  only **one** strict BF8 root. It is retained as a navigation diagnostic,
  not round coverage or a passed 256-call BF8 gate.
- The JOB=2 retry isolates BLEND_LISTS=1 with the other new stage flags=0,
  leaving leaf mask 827, GEOMETRY=1 and Stage F=1 unchanged. Native buzzer
  commands are spaced 1.5 seconds apart. The private helper can recover
  profile selection with further native mailbox commands until the strict
  counter reaches 256; no recovery attempt was needed in the successful run.
  `round_start_s=160.4` and `bf8_strict_gate_s=160.4` are both recorded.
  It then runs another 60 seconds, recording 3,283 frames through
  219,797.958 ms, **at least 40,000 strict/native BF8 roots** and **2,175,000
  strict whole-job/I/O comparisons**, without differences. The readback shows
  an actual round question with the host/player, stage, answers and timer.

All sessions completed. No compilation, CPU tests or profiling overlap these
functional runs. The original save (`54f28fdc...`) and canonical private
template (`92ed9caf...`) hashes are unchanged. Private CSV/stderr pairs are
`analysis/vk-scene-p3-blend-lists-v1-{menu,round,checked-round,root-retry}.*`;
renderer PPM/PNG readbacks use the same tags under `phase3`. Source, EXE/map,
save and archived evidence hashes are bound by private
`phase3/blend-lists-v1-build.json`.

These are correctness/render observations, not matched FPS or CPU results.
The subsequent run-list section addresses the `0x7118` data dependency.
Four cold typed-family gates, remaining enclosing bodies and final
interpreter/performance/default decisions still keep phase 3 open.
BLEND_LISTS and the whole-job driver remain default-off.

## Complete native run-list decoder 7118/7298 (2026-10-31)

`port/native_geometry_run_lists.h` implements the complete in-place data
decoder called once per selected BF8 stream. A BE16 control contains an
11-bit starting index and a five-bit run length minus one; it expands to
successive BE16 offsets multiplied by 16. Native preflight verifies source
and output ranges, the total expanded count and the original constant tables
before mutation. Count zero preserves the original early-return contract,
including arbitrary unused base/count arguments.

The host walks the captured controls backward and writes the output ramps
directly. The final two pipeline steps reconstruct every observable ABI
vector, load and masked store, preserving original in-place aliases, initial
tail padding and wrapped speculative reads. All upper GPR lanes retain their
entry dependencies; neither the native body nor its final steps dispatches
lifted `0x7118/7298` blocks. The lexical fallthrough to `0x7390` after the
unconditional SPU_RET is unreachable and is excluded from the oracle's
dependency closure.

`BUZZ_GEOMETRY_JOB_RUN_LISTS=0/1/2` selects original/native/strict root,
default **0**. Mode 2 also checks a native JOB=1 invocation without requiring
whole-job shadow I/O because this root performs no DMA or channel operations.
JOB=2 automatically checks it from the same full input. All GPRs, all LS,
architectural controls, return PC/FN, image/depth/policy/overlay must match;
there is no new exception. Return-tail identity and an early return whose
link equals `0x7298` are both handled. Owner/foreign/original/off routing is
retained, and unsupported inputs fall back before writes. Adapter protocol
**17** and stage counter 22 route this additional kernel independently of BF8.

Separate counters report native/strict **nonempty expansions** and **empty
returns**. A successful empty root does not validate the data path.

Validation:

```powershell
.\tests\scripts\test-geometry-run-lists.ps1
.\tests\scripts\test-geometry-job-runtime.ps1
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
```

PASS: **10,774 strict root and independent numeric cases**, including every
one of the **65,536 control encodings**, mixed run lengths, single/two-run
tails, lists up to 4,096 controls, full GPR entropy, both host depths,
return-link/tail collision, zero early returns with invalid unused arguments,
and speculative reads wrapping at the end of LS. **17 unchanged-state/no-I/O
rejects** cover ranges/counts, all nine table rows, the shuffle mask and a
foreign LS. The pinned private oracle contains two complete original blocks
with no reachable external callees.

Driver tests cover owner/off/foreign/null-tail/unsupported/post-scope routing,
strict JOB=2/RUN_LISTS=1 and JOB=1/RUN_LISTS=2, and separate empty/nonempty
accounting, alongside 1,024 existing pipeline models and all 19,508 real
entries. The 43 controller scenarios and **168 production-getter cases** pass.
The configuration test script now returns success after deliberate rejection
tests instead of retaining their final expected nonzero process exit code.

Frozen `build-vulkan-phase3-run-lists-v1`:
EXE SHA-256 `88acbc3924ccedd808d7a3fdc08a4a6d60a7bef8a99a0c04f33c09e9046a144a`;
map SHA-256 `d21c5c501855ecd938ee70c79174f20c6deb6bd91f7478a6da30787943049b13`.
Private generated source: `phase3/frozen-run-lists-v1-source.c`.

All five corpus configurations pass **19,508 exact jobs / 58,195 transfers**,
zero changed-input jobs and zero failures, with Stage F=2, mask 827 and no
replay-contract override:

| Configuration | JOB | Other new flags | RUN_LISTS | Native expansions / empty / fallback | Strict expansions / empty |
|---|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline | 0 / 0 |
| Independent | 2 | 0 | 1 | 895 / 303 / 0 | 895 / 303 |
| Combined native | 1 | 1 | 1 | 895 / 303 / 0 | 0 / 0 |
| Combined checked | 2 | 1 | 1 | 895 / 303 / 0 | 895 / 303 |
| Kernel checked | 1 | 1 | 2 | 895 / 303 / 0 | 895 / 303 |

Both JOB=2 configurations also pass 19,508 complete outer-job/I/O comparisons.
BF8 remains native for all 328 roots in combined configurations, without
fallback; previous codec coverage is unchanged. The four cold typed families
remain zero-call real-data paths with their gates explicitly open.

Native/live/render checks use that same frozen EXE, fresh private saves,
all earlier new stage flags=1, GEOMETRY=1, Stage F=1 and leaf mask 827:

- JOB=1/RUN_LISTS=1 menu, 75 seconds: 2,284 CSV frames through 74,836.890 ms;
  inspected native readback shows the normal menu, studio, crowd and UI.
- JOB=1/RUN_LISTS=1 round, 60 seconds after `round_start_s=136.6`: 6,048
  CSV frames through 196,312.309 ms. Logged native counters reach at least
  **540,000 nonempty expansions** and **245,000 empty returns**. The readback
  shows an actual round question, answers, timer, host/player and green stage.

- JOB=1/RUN_LISTS=2 strict round, 90 seconds after `round_start_s=147.1`:
  6,254 CSV frames through 236,645.880 ms. It records at least **580,000
  strict nonempty expansions** and **260,000 strict empty returns**, without
  differences. The final inspected native readback shows an actual question,
  answers/timer, host and animated player on the round stage. Empty calls
  are reported separately and do not count toward the 256-call data gate.

All runs completed. No compilation, CPU tests or profiling overlap these
functional checks. The original save (`54f28fdc...`) and canonical private
template (`92ed9caf...`) hashes are unchanged. Private CSV/stderr pairs use
`analysis/vk-scene-p3-run-lists-v1-{menu,round,kernel-checked-round}.*`, and
renderer PPM/PNG readbacks use the corresponding tags under `phase3`.
Private `phase3/run-lists-v1-build.json` binds source/test/document hashes,
the frozen EXE/map/generated C, original save pins and archived evidence.

These are correctness/render observations, not matched FPS or CPU-cycle
results. RUN_LISTS and JOB remain default-off pending final performance/
default work. Required built-in raw formats 1..5, remaining enclosing stage
bodies, the four cold typed-family live gates, interpreter counts and
reversed same-EXE FPS/normalized worker/process CPU measurements keep
phase 3 unfinished. Strict Stage F cleanup remains final phase 7.

## Native custom input dispatch 93F0 to 7DF8 (2026-10-31)

The phase-1 inventory contains only input-format pairs `(0,255)` in 18,936
jobs and `(0,0)` in 572. The existing 11,414 `raw-stream` fallbacks therefore
must not be attributed to built-in formats 1..5. The new increment replaces
that real custom-descriptor path, while keeping unimplemented built-ins
explicitly separate in the inventory.

`port/native_geometry_custom_stream.h` owns the complete nonempty custom
dispatch: context/header reads, original 448-byte frame and GPR spills,
argument construction, frame restoration and the queued `0x7DF8` tail.
No original `0x93F0/9408/9518/9580/95DC/9668` control block runs on the native
path. It preserves all unused vector lanes and dynamic code-table reads,
even though some values/spills can be overwritten by the descriptor decoder.
The pending callee and PC are exact before its frame begins.

Eligibility requires a valid physical LS, aligned safe stack/context,
nonzero vertex count, a format byte >=6, zero preferred callback word in
input r8 and a nonzero descriptor in r7. Before any writes, other cases
delegate to the existing raw/native-or-original path. Opaque callbacks,
error paths and built-in formats are not relabeled as native custom work.
`BUZZ_GEOMETRY_JOB_CUSTOM_STREAM=0/1/2` selects original/native/strict handoff,
default **0**, independently of ATTRIBUTES and the descriptor/data switches.
The adapter requires protocol **18**, with stage counter 23 (24 counters).

JOB=2 or CUSTOM_STREAM=2 replays the complete original dispatcher on a clone,
stopping by **callee identity** at `0x7DF8` before executing its data body.
All GPRs, all 256 KiB LS, architectural controls, PC, pending function,
image/depth/policy/overlay must match at that function boundary. No field or
dead spill is excluded. Descriptor iteration and payload code keep their
separate correctness/native coverage; full-job replay additionally checks
the composed dispatcher/data flow and every transfer. The handoff counter
does not by itself claim native descriptor payload coverage.

Validation:

```powershell
.\tests\scripts\test-geometry-custom-stream.ps1
.\tests\scripts\test-geometry-job-runtime.ps1
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
```

PASS: **5,000 strict state/handoff cases and 13 unchanged/no-I/O rejects**.
The pinned oracle contains all six original dispatch blocks, with the data
tail and unsupported zero/error paths trapped. Tests cover every custom
format byte 6..255, odd/even counts through 65,535, arbitrary GPR/LS bits,
nonzero upper callback lanes, biased descriptor/source arguments, both
interrupt states, three host depths and a saved return link equal to the
handoff PC. Native arguments are also checked independently against the
descriptor/count contract. Rejections cover context/stack aliases and bounds,
zero counts/descriptors, fixed formats, callbacks and foreign LS.

PASS: owner/off/foreign/null-tail/callback/error/fixed/zero/post-scope routes,
independent ATTRIBUTES=0 activation, JOB=2/CUSTOM_STREAM=1 and
JOB=1/CUSTOM_STREAM=2, all earlier routes, 1,024 pipeline models and 19,508
real entry models. The 43 controller cases and **176 configuration cases**
pass; mode 2 is explicitly accepted by the production getter.

Frozen `build-vulkan-phase3-custom-stream-v1`:
EXE SHA-256 `5775a3c6ed644bede11ca54cc4b525759e6c3a3403ba05467231d97aa7711074`;
map SHA-256 `b0e766c741ef2b5182fa694b3738d3357d2f4b318818fc3eab233eae51dfd738`.
Private generated source: `phase3/frozen-custom-stream-v1-source.c`.

All five corpus configurations pass **19,508 exact jobs / 58,195 transfers**,
zero changed inputs and zero failures, with Stage F=2, mask 827 and no
replay-contract override:

| Configuration | JOB | Other new flags | CUSTOM_STREAM | Native handoffs / fallback | Strict handoffs |
|---|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline | 0 |
| Independent | 2 | 0 | 1 | 11,414 / 0 | 11,414 |
| Combined native | 1 | 1 | 1 | 11,414 / 0 | 0 |
| Combined checked | 2 | 1 | 1 | 11,414 / 0 | 11,414 |
| Handoff checked | 1 | 1 | 2 | 11,414 / 0 | 11,414 |

Combined configurations have **19,508 native raw paths / zero fallback**,
instead of counting these custom handoffs as raw fallback. The 12,309 native
descriptor controllers and all previous real codec coverage remain unchanged.
No built-in 1..5 data path was exercised or validated by this change.

Live checks use that same frozen EXE, fresh private saves, all earlier new
stage flags=1, GEOMETRY=1, Stage F=1 and leaf mask 827:

- JOB=1/CUSTOM_STREAM=2 checked menu, 60 seconds: 1,430 CSV frames through
  59,310.863 ms and **at least 410,000 strict full-state/handoff matches**.
  The original data tail is not executed a second time by this checker.
- JOB=1/CUSTOM_STREAM=1 native menu, 75 seconds: 2,289 frames through
  74,854.605 ms. The native frame readback shows the normal menu, UI, crowd
  and studio stage.
- JOB=1/CUSTOM_STREAM=1 native round, 60 seconds after
  `round_start_s=136.5`: 6,077 frames through 196,129.321 ms and **at least
  5,035,000 native custom handoffs**. The final readback shows the actual
  green round stage, host, player and scoreboard.

All sessions completed without DIFF/HALT/mismatch. No builds, CPU tests or
profiling overlapped the functional runs. The original save (`54f28fdc...`)
and canonical private template (`92ed9caf...`) hashes remain unchanged.
Private CSV/stderr pairs use
`analysis/vk-scene-p3-custom-stream-v1-{checked-menu,menu,round}.*`;
native PPM/PNG frames for menu/round use the same tags under `phase3`.
`phase3/custom-stream-v1-build.json` binds source/test/docs, frozen EXE/map/
generated C, original saves and archived evidence.

These are correctness/render observations, not matched FPS or CPU-cycle
measurements. Performance/default decisions, built-in formats 1..5, cold
typed families, remaining enclosing bodies and final interpreter/reversed
A/B/normalized CPU gates still keep phase 3 open. All new defaults remain
off; strict Stage F cleanup remains final phase 7.


## Native skin/rigid dispatch 9F8 (2026-10-31)

`port/native_geometry_skin_dispatch.h` replaces all eight reachable controller
blocks (`9F8/A28/A84/B18/B48/B54/B80/B9C`) with host branching, shared frame
save/restore and explicit library calls. It preserves the original 160-byte
frame, all vector lanes, register state before every callee, link/entry PCs,
image/depth and pending continuation. The lexical BF8 branch after B9C's
unconditional B48 branch is unreachable and is not a dependency.

The preferred low byte of input r9 selects weighted data when zero, including
nonzero words such as `0x10000`. The controller constructs r5/r6 and restores
its frame before queuing AF18. On the rigid path it calls the three pointer
getters, transforms positions through C518, then conditionally transforms and
normalizes normals and tangents. The secondary-stream test observes bit zero
of `r10^1`, not whether the entire byte differs from 1. It returns through
B18 when no tangent needs work, otherwise restores its frame and queues the
final 2C00 normalization. Both existing Stage F calls keep the accepted
baseline contract; no new state exception is added.

`BUZZ_GEOMETRY_JOB_SKIN_DISPATCH=0/1/2` selects original/native/strict controller
boundaries, default **0**. JOB=2 also forces the strict boundary check. The
reference executes on a clone, stopping by function identity at AF18 or the
final 2C00 tail, or at the original r0 return. Synchronous child normalization
calls are drained internally. Weighted data and the final normalization are
not executed twice by the checker. All 128 GPRs, 256 KiB LS, architectural
controls, PC, pending function, image/depth/policy/overlay must match. Separate
native/strict counters distinguish weighted handoffs from rigid execution;
neither implies native AF18 data coverage. Adapter protocol **19**, stage
counter **24** (25 counters), is required by CMake.

Preflight requires physical LS, an aligned manager >=E680 with a `0x170`-byte region,
an aligned stack >=E840 with space through stack+31, and a disjoint 192-byte
controller frame/link span. Missing callbacks and unsupported input delegate
to the original before mutation. The existing switches for child routines
remain independent.

```powershell
.\tests\scripts\test-geometry-skin-dispatch.ps1
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
.\tests\scripts\test-geometry-job-runtime.ps1
```

PASS: **6,144 strict full-state/boundary/call-trace cases**: 3,072 weighted
handoffs, 2,304 rigid returns, 768 normal transforms/normalizations and 768
tangent tail handoffs (normal/tangent categories overlap). The pinned oracle
contains eight complete original controller blocks and five adversarial
library models. Models hash all argument lanes and entry PC/depth into
volatile-register/LS effects; independent assertions check the call sequence,
arguments and entry PCs. Tests cover flags, all nonzero selector bytes,
arbitrary GPR/LS bits, pointer biases, three depths, both interrupt states,
and r0 collisions with internal/tail PCs. Eight invalid/overlap/foreign-LS
cases reject without mutation or I/O.

PASS: owner/native, both strict activation modes and weighted/return/normalize
boundaries; disabled/foreign-image/foreign-context/missing-table/missing-callee/
invalid-stack/post-scope routes; all previous routes, 1,024 pipeline models
and **19,508 real entry models**. All **43 controller cases and 184 production
configuration-getter cases** pass.

Frozen `build-vulkan-phase3-skin-dispatch-v1`:
EXE SHA-256 `ee02802d7fade1854a214e90a490511010c3dfc9b766fa8cc924d37f72dc408e`;
map SHA-256 `86340e6e97ad4b8750d86bc7d2029c5209559337563cc9b964b5d01ee77cc296`;
generated C SHA-256
`3049b8c1561428c4d41db9e6a0419de013dcac0ade39e32989781bba4c2ed08d`,
preserved as `phase3/frozen-skin-dispatch-v1-source.c`.

All five configurations pass **19,508 exact jobs / 58,195 transfers**, zero
changed inputs and zero failures. Stage F=2, leaf mask 827 and no replay
contract override apply throughout:

| Configuration | JOB | Other new flags | SKIN_DISPATCH | Native weighted / rigid | Strict weighted / rigid | Fallback |
|---|---|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline | 0 / 0 | n/a |
| Independent | 2 | 0 | 1 | 1,628 / 17,880 | 1,628 / 17,880 | 0 |
| Combined native | 1 | 1 | 1 | 1,628 / 17,880 | 0 / 0 | 0 |
| Combined checked | 2 | 1 | 1 | 1,628 / 17,880 | 1,628 / 17,880 | 0 |
| Controller checked | 1 | 1 | 2 | 1,628 / 17,880 | 1,628 / 17,880 | 0 |

The two JOB=2 arms additionally pass 19,508 whole-job state/I/O comparisons.
Combined counters retain 11,414 custom handoffs, 12,309 descriptor roots,
328 BF8 roots, 1,198 run-list roots and 2,326 weighted scatters, with their
previous real codec coverage. The strict controller does not duplicate AF18
DMA or the final normalization data tail. These results establish controller
compatibility, not a new native implementation of either data tail.

Live validation uses that same frozen EXE, fresh copies of the canonical
private save, all older stage flags=1, GEOMETRY=1, Stage F=1 and leaf mask 827.
JOB=1/SKIN_DISPATCH=2 checked menu completes 60 seconds, with 2,094 CSV frames
through 59,817.239 ms and **at least 300,000 strict rigid boundaries plus
135,000 strict weighted handoffs**. Both paths independently exceed 256 calls.
JOB=1/SKIN_DISPATCH=1 native menu completes 75 seconds, with 3,094 frames
through 75,105.009 ms, at least 2,315,000 native rigid calls and 1,045,000
weighted handoffs. The inspected 1280x720 readback shows the normal menu UI,
crowd and studio geometry. Neither run logs a DIFF/HALT/mismatch.

JOB=1/SKIN_DISPATCH=1 native round completes 60 seconds after
`round_start_s=136.5`: 8,595 frames through 196,166.064 ms, **at least 6,565,000
native rigid calls and 1,955,000 weighted handoffs**. The inspected 1280x720
readback shows the actual green question/round stage, host, player, answers
and scoreboard. No DIFF/HALT/mismatch appears.

All test/build/corpus/live sessions are terminal. No build, CPU test or
profiling overlapped the functional live runs. Original lift/generator and
save/template pins remain unchanged. Private CSV/stderr pairs use
`analysis/vk-scene-p3-skin-dispatch-v1-{checked-menu,menu,round}.*`; native
PPM/PNG readbacks use the menu/round tags under `phase3`. The source-bound
`phase3/skin-dispatch-v1-build.json` and its archived proofs preserve the
frozen EXE/map/generated source, tests, logs, frames and committed docs.

These functional runs do not establish a matched FPS or CPU-cycle benefit.
All new defaults remain off (JOB=0, leaf mask 827). AF18/B880, other enclosing
data stages, built-in input formats 1..5, the cold typed real/live gates and
final interpreter/reversed A/B/normalized worker/process CPU/default decisions
keep phase 3 open. The next implementation target is AF18's palette transpose/batch
flow, with full strict state and I/O checking. Strict Stage F reconstruction
remains final phase 7.


## Complete weighted skin position/matrix data B880 (2026-10-31)

Auditing AF18 identifies a pure-LS skin pipeline with explicit position,
cofactor/normal and directional data kernels, plus palette transposition and
batch control. No DMA instruction occurs on its valid paths. This corrects
the earlier placeholder description; the upstream loader stages the data.
The current inventory lists all of these dependencies separately. The next
native increment replaces the complete hot weighted position/matrix region,
rather than depending on its original setup and final pair.

`port/native_geometry_weighted_skin.h` processes pairs with AVX2, preserving
FM/three-FMA influence order and all four palette reads even for zero weights.
Its final pair computes the weighted matrices, positions and every live or
scratch GPR in native code. It also reconstructs lookahead coefficients,
addresses, unused pointer/count lanes and the exact B4C0/BB58 continuation.
No original B880/B934/B940 block executes on the eligible native path.
Single-pair counts and all 16 pointer biases are supported, with quadword-floor
LS semantics. Preflight checks data/lookahead/palette extents, all gather/mask/
scale constants and writable-stream/input aliases before any mutation. Actual
palette/output aliases retain original pair ordering and have separate
native/strict coverage counters. Unsupported
states retain the prior phase-2 path, preserving its own leaf switch.

`BUZZ_GEOMETRY_JOB_WEIGHTED_SKIN=0/1/2` selects prior/native/strict data-state
execution, default **0**; JOB=2 also forces strict mode. The pure original
three-block reference runs on a clone and stops before either data tail.
The comparison covers all GPR/LS/architectural controls, PC and pending
function, image/depth/policy/overlay, without exclusions. Independent counters
distinguish one-pair and bulk calls; fallbacks are separate. The adapter now
requires protocol **20**, stage counter **25** (26 counters).

```powershell
.\tests\scripts\test-geometry-weighted-skin.ps1
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
.\tests\scripts\test-geometry-job-runtime.ps1
```

PASS: **6,912 strict full-state/boundary cases and 12 unchanged/no-I/O rejects**.
Twenty count edges span 2..2,048 vertices. Tests vary all pointer biases,
independent palette-row bases, all 256 indices, all-zero/each one-hot/sparse/
dense/non-normalized weights, arbitrary FP payloads and upper register lanes,
three host depths, both interrupt states and both continuation branches.
The original oracle and constants are pinned independently of the new kernel.
An additional 256 cases place writable positions in gaps between actual
indexed palette quads. Another 256 exercise true palette/output feedback with
nonzero weights across multiple pairs. Preflight checks every indexed quad,
including zero-weight indices. Palette aliases are supported by loading all
six matrix rows for a pair before its first store; subsequent pairs observe
those stores. Writable position/weight/output-stream aliases remain guarded.
This preserves the complete original state without exempting any alias data.

PASS: one-pair and bulk routing, both strict activation modes, both tails,
owner/off/foreign-image/foreign-context/missing-reference/missing-tail/invalid/
post-scope cases, previous routing checks, 1,024 pipeline models and 19,508
real entry models. **43 runtime cases and 192 production getter cases** pass.
The first full combined checked replay on frozen V3 passes all **19,508 exact
jobs / 58,195 transfers**, zero changed inputs or failures. All **6,113 weighted
data roots execute natively with zero fallback**, including **30 actual
palette/output aliases**; every data root and alias passes the full-state
boundary comparison. The complete JOB=2 execution also passes 19,508
whole-job state/I/O comparisons. The ordinary reference execution retains
its prior leaf behavior; its old phase-2 fallback counter is independent of
the new region's zero-fallback counter.

Frozen `build-vulkan-phase3-weighted-skin-v3`:
EXE SHA-256 `37dc73a1bc702bf217c665aa0bd218ec0735313a4352ef98b78125872e7fb814`;
map SHA-256 `149fabddc77d7eefdf6d62bf44b478283700087af8bdbda81019bb2a3b78fbb6`;
generated C SHA-256
`91601b5474c4e828039ad13959f9e3f3bcf8f2a1e9465d0d54f48d195185099c`.
The generated C is preserved as `phase3/frozen-weighted-skin-v3-source.c`;
frozen copies of the compiled native header/controller sources bind changes
that do not alter the adapter's C text.

The final footprint/order behavior has two diagnostic predecessors. V1 used
a conservative palette bounding span and reported 5,700 native / 413 fallback.
V2 checked the indexed quads and reported 6,083 native / 30 fallback. Both
passed replay, but neither proved complete native coverage. V3 supports the
remaining actual aliases with original pair ordering and no state waivers.
Their frozen EXEs/logs remain private diagnostic evidence; acceptance uses
V3 exclusively. Single-pair native data is validated synthetically; no
single-pair call occurs in this real corpus.

All five V3 configurations pass **19,508 exact jobs / 58,195 transfers**,
zero changed inputs and zero failures, Stage F=2 / leaf mask 827 / no contract
override:

| Configuration | JOB | Other new flags | WEIGHTED_SKIN | Native data / fallback | Strict data | Native / strict palette aliases |
|---|---|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline | 0 | 0 / 0 |
| Independent | 2 | 0 | 1 | 6,113 / 0 | 6,113 | 30 / 30 |
| Combined native | 1 | 1 | 1 | 6,113 / 0 | 0 | 30 / 0 |
| Combined checked | 2 | 1 | 1 | 6,113 / 0 | 6,113 | 30 / 30 |
| Kernel checked | 1 | 1 | 2 | 6,113 / 0 | 6,113 | 30 / 30 |

Both JOB=2 arms additionally pass 19,508 whole-job state/I/O checks; previous
real payload/controller counts remain unchanged. The final 6,912-case test
also varies physically noncontiguous palette rows, as supported by the API.

Live checks use that same frozen V3 EXE, fresh private-save copies, all older
stage flags=1, GEOMETRY=1, Stage F=1 and leaf mask 827. The 60-second
JOB=1/WEIGHTED_SKIN=2 menu run completes 2,061 CSV frames through
59,685.196 ms, with **at least 430,000 strict weighted data boundaries and
256 strict actual palette/output aliases**. No DIFF/HALT/mismatch or fallback
is logged. Single-pair counts remain absent from this live workload.

The 75-second JOB=1/WEIGHTED_SKIN=1 native menu completes 2,997 frames through
74,756.869 ms, with **at least 3,860,000 native weighted data calls and 256
native palette/output aliases**. The inspected 1280x720 readback shows normal
menu UI and studio/stage geometry. No DIFF/HALT/mismatch or fallback appears.

The 60-second JOB=1/WEIGHTED_SKIN=1 round run starts at
`round_start_s=136.4` and completes 8,437 CSV frames through 195,994.520 ms.
It records **at least 7,515,000 native weighted data calls and 40,000 native
palette/output aliases**. The inspected 1280x720 readback shows the actual
green question/round stage, host, player, answer panels and scoreboard.
No DIFF/HALT/mismatch or fallback is logged.

All build/test/corpus/live sessions are terminal. No compilation, CPU test
or profiling overlapped the functional live runs. Original lift/generator,
original save and canonical private-template pins remain unchanged. Private
CSV/stderr pairs use `analysis/vk-scene-p3-weighted-skin-v3-{checked-menu,menu,round}.*`;
menu/round PPM/PNG readbacks use the same tags under `phase3`. The source-bound
`phase3/weighted-skin-v3-build.json` and archived proofs bind the committed
code/tests/docs, frozen EXE/map/generated C/native sources, real LS-table
fixture, static audit, logs and frames.

These functional observations establish no matched FPS or CPU-cycle benefit.
JOB=0, leaf mask 827 and all new stage flags=0 remain unchanged. AF18
palette/batch control, rigid skin position data, cofactor normals, six
remaining directional kernels, other enclosing stages, built-in formats
1..5, cold typed real/live gates and final interpreter/reversed A/B/normalized
worker/process CPU/default decisions keep phase 3 open. Continue with these
explicit data/controller dependencies; strict Stage F cleanup stays final
phase 7.


## Complete basic skin direction data BD4C/BE20/BF7C (2026-10-31)

The real phase-1 trace contains **1,158 BD4C roots and 3,860 BE20 roots**;
BF7C and the normalized/cofactor families have zero real entries. The
reproducible skin audit now emits these per-root counts and their source
trace hash alongside its source/fixture-pinned 38-block map. The validated
trace denominator remains 19,508 jobs and 58,195 transfers.

`port/native_geometry_skin_directions.h` implements complete one/two/three-
stream unnormalized data kernels. Each uses a host group loop and AVX2 pairs
for FM/two-FMA matrix arithmetic. First-group matrix/primary vectors come
from the input GPR cache, even if LS contains different values. Following
primary/matrix groups are prefetched before stores; secondary/tertiary inputs
are gathered before the current outputs. Stream/matrix aliases retain these
dependencies and the original stream store order. The final native state
materializes every variant-specific intermediate and unused register lane,
including the speculative next group, pointer/count arithmetic and pending
B868 continuation. No original data/setup/step/final block runs natively.

`BUZZ_GEOMETRY_JOB_SKIN_DIRECTIONS=0/1/2` selects original/native/full-state
comparison, default **0**. JOB=2 also enforces the independent boundary check.
A reference clone runs the nine pinned original blocks, stopping by callee
identity at B868 before the batch continuation. All GPR/LS/architectural
control/PC/pending-function/image/depth/policy/overlay fields must match;
there are no state exclusions or new Stage F exceptions. Counters distinguish
1/2/3 streams and aliases; fallback is separate. Adapter protocol **21**,
stage **26** (27 counters), is required by CMake.

Eligibility is established before any write: physical LS, 1..3 streams,
positive multiple-of-four count <=4,096, and all actual writes/lookahead
reads within the supported LS data range. It supports all quadword pointer
biases and distinct first/nominal-primary bases. Unsupported shapes or missing
tails retain the original. It does not impose a first-group LS/GPR equality
assumption or require disjoint streams/matrix scratch.

```powershell
.\tests\scripts\test-geometry-skin-directions.ps1
.\tests\scripts\test-geometry-driver-flow.ps1 -Corpus analysis/native-geometry/replay-final/all/image41
.\tests\scripts\test-geometry-job-runtime.ps1
.\toolenv\Scripts\python.exe tools/inspect/inspect-geometry-skin.py
```

PASS: **6,912 strict full-state/boundary cases, three independent numeric
identity/cache cases and 12 unchanged/no-I/O rejects**. Each stream-count
variant has 2,048 random cases and 256 targeted overlap/pointer cases; 640
of the latter actually overlap streams or matrix inputs. Counts span 4..2,048
vertices, with arbitrary FP payloads, selector bytes, upper lanes, prefetched
GPR inputs, all pointer biases, three host depths and both interrupt states.
Independent numeric cases verify identity transforms and deliberately differ
the primary GPR cache from LS. The complete original oracle is source-pinned
and its B868 tail is trapped.

PASS: all three variants' owner/native, both strict activation modes, exact
tail identity (including r0 collisions), disabled/foreign-image/foreign-context/
missing-tail/invalid-shape/post-scope routing, previous routes, 1,024 pipeline
models and 19,508 real entry models. **43 runtime cases and 200 production
configuration-getter cases** pass.

The first frozen combined checked replay passes **19,508 exact jobs / 58,195
transfers**, zero input changes or failures, with **5,018 native data roots /
zero fallback**. All 1,158 single-stream and 3,860 two-stream roots pass strict
state/boundary checks, alongside 19,508 complete job state/I/O comparisons.
Three-stream real/native/strict counts remain zero; its real/live gates are
OPEN. The source trace records 15,364 BD58 and 143,551 BE30 group entries;
the new host loops replace those original group/step trampolines. These are
execution counts, not a timing or FPS claim.

Frozen `build-vulkan-phase3-skin-directions-v1`:
EXE SHA-256 `d30c408a05e8fab2c33775209391f823dd4ba3e6a3f97e1ed6dc78f7aae162de`;
map SHA-256 `97e57c2f7508c8fa5424eb9a59fd80d6d6798ea04d65ae2c5a233aa7522e5f4d`;
generated C SHA-256
`4f1359dc961afd25cfaf46da8b6216b5900fcfe9a6e7fb82497457f4074bec34`,
preserved as `phase3/frozen-skin-directions-v1-source.c` with frozen copies
of the four compiled native header/controller sources.

All five frozen configurations pass **19,508 exact jobs / 58,195 transfers**,
zero input changes and zero failures, Stage F=2 / leaf mask 827 / no contract
override:

| Configuration | JOB | Other new flags | SKIN_DIRECTIONS | Native 1 / 2 / 3 streams | Strict 1 / 2 / 3 streams | Fallback |
|---|---|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline | 0 / 0 / 0 | n/a |
| Independent | 2 | 0 | 1 | 1,158 / 3,860 / 0 | 1,158 / 3,860 / 0 | 0 |
| Combined native | 1 | 1 | 1 | 1,158 / 3,860 / 0 | 0 / 0 / 0 | 0 |
| Combined checked | 2 | 1 | 1 | 1,158 / 3,860 / 0 | 1,158 / 3,860 / 0 | 0 |
| Kernel checked | 1 | 1 | 2 | 1,158 / 3,860 / 0 | 1,158 / 3,860 / 0 | 0 |

Both JOB=2 arms additionally pass 19,508 whole-job state/I/O comparisons.
Existing weighted, descriptor, stream, scatter, list and publication counts
are retained. All real directional alias counts are zero in this corpus;
the overlapping cases remain synthetic coverage.

Live checking uses that same frozen EXE, fresh private saves, all older flags=1,
GEOMETRY=1, Stage F=1 and leaf mask 827. JOB=1/SKIN_DIRECTIONS=2 checked menu,
60 seconds, completes 2,213 frames through 59,642.454 ms and **at least
525,000 strict single-stream checks**, with no DIFF/HALT/mismatch or fallback.
Two/three-stream calls are zero in the menu; this run does not satisfy their
live gates. JOB=1/SKIN_DIRECTIONS=1 native menu, 75 seconds, completes 3,356
frames through 74,772.473 ms with **at least 2,565,000 native single-stream
calls**, no two/three-stream calls, no DIFF/HALT/mismatch and no fallback.
The inspected 1280x720 readback shows normal menu UI, studio and crowd.

JOB=1/SKIN_DIRECTIONS=1 native round completes 60 seconds after
`round_start_s=136.5`: 9,695 frames through 196,256.476 ms, **at least
3,470,000 native single-stream calls and 1,805,000 two-stream calls**.
The inspected 1280x720 readback shows the actual green round stage, host,
player and score display. No DIFF/HALT/mismatch or fallback appears;
three-stream calls remain zero.

A separate JOB=1/SKIN_DIRECTIONS=2 checked round completes 60 seconds after
`round_start_s=136.5`: 6,130 frames through 196,449.823 ms, **at least 1,125,000
strict single-stream and 680,000 strict two-stream comparisons**. Both
observed functions independently exceed 256 live calls. Its inspected
1280x720 readback shows the actual green question/round stage, host, player,
answer panels and score display. No DIFF/HALT/mismatch or fallback appears.
Three-stream counts remain zero in every real/live run.

All build/test/corpus/live sessions are terminal. No compilation, CPU test
or profiling overlapped the functional live runs. Original lift/generator,
original save, canonical private template and LS-table fixture pins remain
unchanged. Private CSV/stderr pairs use
`analysis/vk-scene-p3-skin-directions-v1-{checked-menu,menu,round,checked-round}.*`;
menu/round/checked-round PPM/PNG readbacks use the same tags under `phase3`.
`phase3/skin-directions-v1-build.json` and archived proofs bind committed
code/tests/docs, frozen EXE/map/generated C/native sources, static/real trace
audit, test binaries/logs and frames.

These functional runs establish no matched FPS or CPU-cycle benefit. JOB=0,
leaf mask 827 and all new flags=0 remain unchanged. BF7C's real/live gates
stay OPEN; a common-switch default decision must retain that distinction.
At that milestone, normalized C14C/C280/B5A0, cofactor/normal BB58/BB68
and AF18 palette/batch/publication control were still original. The following
section replaces AF18 control; the data and other inventory rows remain open. Built-in formats, cold typed gates, final interpreter counts,
reversed A/B and normalized worker/process CPU/default decisions also remain
open. Strict Stage F reconstruction stays final phase 7.


## Native AF18 palette, batch and publication orchestration

`port/native_geometry_skin_flow.h` implements the complete AF18 controller:
672-byte frame and early exits, in-place packed palette transpose, stream
selection/compaction, scratch-pool capacity, B348 batch clamps, B4C0 relative
table dispatch, B868 continuation, palette publication calls and return.
Palette conversion and batch traversal use host loops; fixed sequences
materialize every original ABI vector, padded read and spill. The new path
executes no original palette/control block. Position/direction data and the
five ordinary pure-LS calls remain explicit library callbacks. Validated
weighted/basic-direction native kernels retain their own switches/counters;
rigid, cofactor and normalized data remain original, separately tracked work.

`BUZZ_GEOMETRY_JOB_SKIN_FLOW=0/1/2` defaults to **0**. Its mode 2 and JOB=2
compare the complete function against a source-pinned original clone,
including all 128 GPRs / all LS, architectural state, PC, queued function, image,
host depth, policy and overlay. Reference termination requires both the
original r0 and restored stack, preserving interior/entry PC collisions.
No exclusions or Stage F exception are added. Protocol 22 / stage 27 have
28 counters, with independent work/empty and native/strict categories.
Unsupported/error paths are rejected before mutation, then execute original
AF18 without native-success credit. Protected manager/frame/table regions
remain intact; data/data aliases are left to the explicit data library.

Tests pass **6,600 strict full-state and all-GPR library-call transcript
cases**, including 2,640 work / 3,960 early roots, weighted/rigid batches, all six
directional targets, pointer biases, arbitrary shuffle tables, default-frame
and external scratch, three depths, interrupt states and return-PC collisions.
Three independent numeric cases validate the packed matrix word mapping and
unchanged two-record padding; three early-exit cases prove unused invalid
inputs/table are not inspected. Eighteen rejected-input cases preserve state
and execute no call/I/O. Owner/off/foreign/image/missing-table/invalid/post-scope
routing, both strict activation modes,1,024 outer pipeline models and 19,508
real-entry models pass. The 43 runtime controller cases and 208 configuration-getter cases
pass. One initial routing compilation found an ADL name clash with another
private oracle; explicit namespace qualification fixed it before the frozen
production build. No frozen runtime mismatch was observed.

Frozen `build-vulkan-phase3-skin-flow-v1`:
EXE SHA-256 `d3d33741622b29fcf24808f362824cb3f21f7ed6de2b25d70fb3ad55c96725eb`;
map `94b4204ff236c117daa13d1ed19006594e0fe5a500459a0f5e297ded014a1cc5`;
generated C `4bf8e6386bfcb517b64102e439801f64010b2a7173124e750ea23d9ec2b7b7a0`.
Frozen C and four compiled native header/controller sources are preserved.

All five frozen arms pass **19,508 exact jobs /58,195 transfers**, zero input
changes and failures, Stage F=2 / leaf 827/no contract override:

| Arm | JOB | Older flags | SKIN_FLOW | Native work/empty | Strict work/empty | Controller fallback |
|---|---|---|---|---|---|---|
| Off |0|0|2|No native pipeline|0/0|n/a|
| Independent |2|0|1|1,628/0|1,628/0|0|
| Combined native |1|1|1|1,628/0|0/0|0|
| Combined checked |2|1|1|1,628/0|1,628/0|0|
| Kernel checked |1|1|2|1,628/0|1,628/0|0|

Both JOB=2 arms pass 19,508 whole-job state/I/O checks. Combined-arm data
coverage is retained: 6,113 weighted roots including 30 actual palette aliases,
plus 1,158 single / 3,860 dual-direction roots, all with zero data fallback.
The real trace records 1,628 AF18/palette roots, 6,113 batch/direction dispatches
and zero rigid B3C8, cofactor BB58, normalized C14C/C280/B5A0 or BF7C data
entries. Native controller totals do not turn those cold data paths into
native payload coverage. Empty/control-cold and cold data gates remain OPEN.

Same-frozen-EXE live checks use fresh private saves, all older flags=1,
GEOMETRY=1 / Stage F=1 / leaf 827, without compilation/CPU tests/profiling overlap:

| Functional run | JOB/SKIN_FLOW | Frames /last elapsed ms | Observed work-call lower bound |
|---|---|---|---|
| Checked menu,60s |1/2|2,364 /59,370.738|425,000 strict|
| Native menu,75s |1/1|3,338 /75,066.681|1,265,000 native|
| Native round,60s after start136.5s |1/1|9,617 /196,327.691|2,245,000 native|

No DIFF/HALT/mismatch is logged. Scene shutdown is forced, so final live
fallback totals are unavailable; zero fallback is proved by replay, while
live counters are lower bounds of successful native/strict work. The inspected
1280x720 native menu readback shows normal UI, studio and crowd; the round readback shows the
actual green round stage, host/player and score UI. Empty
work roots remain unobserved in these runs. These are functional/render
checks, with **no matched FPS or CPU-cycle benefit claim**. Final interpreter,
reversed-order menu/round A/B and normalized worker/process cycle/time gates
remain open. Repeatable CPU savings may justify a default even with flat FPS;
this milestone does not yet supply that measurement.

Private logs/CSV use `analysis/vk-scene-p3-skin-flow-v1-{checked-menu,menu,round}.*`;
readbacks and run scripts are under `phase3`. All sessions are terminal,
source/fixture/original-save/canonical-template pins remain unchanged.
`phase3/skin-flow-v1-build.json` and its proof archive bind committed sources,
frozen binary/map/generated C/native sources, tests, real audit and rendering.
JOB=0 / leaf 827 / all new flags=0 remain unchanged. Phase 3 and the continuing goal
remain **active**: rigid/cofactor/normalized kernels, other enclosing stages,
built-in inputs, cold coverage and final gates still require work. Strict
Stage F reconstruction remains mandatory final phase 7.


## Rigid skin data implementation: real/live gates remain open

`port/native_geometry_rigid_skin.h` replaces the complete B348 data suffix,
B3C0 step and B3C8 pair loop. The initial clamp/weighted branch is caller
control; the shared data hook works with either native AF18 or its original
caller, retaining independent `BUZZ_GEOMETRY_JOB_RIGID_SKIN=0/1/2`, default 0.
The native data path calls no original setup, data iteration, step or final
pair. Host AVX2 prefix pairs gather six palette rows and cache the next
positions/indices before writes; final-pair arithmetic materializes all GPRs,
lookahead and pointer/count lanes. All position/index/palette/output aliases,
biased index quads, wrapped gathers and arbitrary masks/shuffles are supported.
The exact B4C0 or BB58 tail remains queued, without executing its data twice.

Mode 2 or JOB=2 checks every GPR/LS byte, architectural/control state, PC,
queued function, image/depth/policy/overlay against the original data closure.
The tail checker validates an allowed (function,PC) pair, including compiler
folding of otherwise identical private trap stubs. No register exclusion or
Stage F waiver is added. Protocol 23 / stage 28 has 29 stage counters, with
bulk/one-pair and potential-alias native/strict categories. Potential aliases
use complete loaded index quads and possible palette spans; they are not a
claim about exact dynamically aliased palette reads. Unsupported inputs
fall back before mutation, with no native credit.

**Synthetic evidence:** 7,681 strict full-state/boundary cases = 6,400 random,
768 targeted aliases/wrapped gathers,512 arbitrary masks/shuffles and 1 maximum
4,096-vertex alias case. The targeted set includes 684 potential-alias candidates.
Three independent translated-identity/padding cases validate position values,
matrix rows, untouched extra positions and the one-byte-per-vertex index
advance. Ten invalid inputs preserve state without I/O. The AF18 harness also
passes 384 strict combined controller/native-rigid cases against actual original
rigid data, with cofactor/direction data explicitly modeled; its earlier
6,600 controller/transcript cases,3 numeric palette checks,3 unused-input early
checks and 18 rejects remain green. Initial integration testing exposed a
missing RTZ setup in that test harness; setting the same RTZ/DAZ/FTZ as the
SPU job runtime makes the full comparison pass, without weakening it.
Owner/off/foreign/image/null-tail/invalid/post-scope routing and both strict
activation modes pass, including one-pair/bulk/alias/two-tail cases. The
1,024 outer models and 19,508 real-entry models pass. Runtime 43 controller
cases,216 configuration cases and a new abrupt-exit first-fallback case pass.

Every owned stage now logs and flushes its first fallback immediately.
`runtime-first-fallback-flush.log` proves this survives `_Exit` without normal
summaries. This improves later live/default evidence; it does not add final
fallback totals to older forced-shutdown runs or turn no-call runs into
successful native data validation.

Frozen `build-vulkan-phase3-rigid-skin-v1`:
EXE `781e139f4983e5f2c5c136121bf0fa7ace5458f652fae05b9e9494ecaa6a1f95`;
map `f3ded77e7ac17d789453a13d1cf7b76d87b78a41d0dfe6de8489cd9aa94a4aa1`;
generated C `4edbe13da32bdb279b9aaf80c4d150cba3e6e4987d6a3eadf0abc8d36e0162ce`.
Frozen C and four compiled native header/controller copies are preserved.

All five configurations pass **19,508 exact jobs /58,195 transfers**, zero
input changes and failures, Stage F=2 / leaf 827/no contract override:

| Compatibility arm | JOB | Older flags | RIGID_SKIN | New native/strict/fallback |
|---|---|---|---|---|
| Off |0|0|2|No native pipeline|
| Independent |2|0|1|0/0/0|
| Combined native |1|1|1|0/0/0|
| Combined checked |2|1|1|0/0/0|
| Kernel checked |1|1|2|0/0/0|

Both JOB=2 arms pass 19,508 whole-job state/I/O checks. Combined arms retain
1,628 AF18, 6,113 weighted roots including 30 palette aliases and 5,018 basic direction
roots, with zero fallback. **The new rigid kernel has zero real-corpus calls.**
The original trace also records B3C8=0. These are compatibility results,
not real-data coverage of the new implementation. Nonzero native corpus
coverage and >=256 strict live comparisons for rigid data remain **OPEN**.
Cofactor BB58/BB68 and normalized C14C/C280/B5A0 are still original work.

Same frozen EXE, fresh private saves, all older flags=1, GEOMETRY=1 / Stage F=1 /
leaf827, with no compilation/CPU tests/profiling overlap:

| Functional compatibility run | JOB/RIGID_SKIN | Frames /last elapsed ms | Rigid native/strict observed |
|---|---|---|---|
| Checked menu,60s |1/2|2,796 /59,864.515|0/0;gate OPEN|
| Native menu,75s |1/1|3,340 /74,808.865|0/0;gate OPEN|
| Native round,60s after start136.5s |1/1|9,592 /196,303.358|0/0;gate OPEN|

The logs confirm requested modes, no DIFF/HALT/mismatch and no observed rigid
native/strict or first-fallback event. Inspected 1280x720 readbacks show normal
menu/studio/crowd and the actual green round with host/player/score UI.
**The scene checks do not validate an uncalled data kernel.** The real/live
rigid gates remain open, regardless of the synthetic cases or normal images.
No matched FPS/CPU benefit is claimed. Final interpreter/reversed A/B and
normalized worker/process cycle/time/default decisions remain open; repeatable
CPU savings may matter even with flat FPS.

Private evidence uses `phase3/rigid-skin-v1-*` and
`analysis/vk-scene-p3-rigid-skin-v1-{checked-menu,menu,round}.*`.
`phase3/rigid-skin-v1-build.json` and archived proofs bind the committed code,
tests/docs, frozen binary/map/C/native snapshots, source trace and scene data.
All runs are terminal and original source/generator/save/template/fixture pins
remain unchanged. JOB=0 / leaf 827 / all new flags=0 remain unchanged. Phase 3 and the
continuing goal remain active; next data work is cofactor and normalized
skinning, followed by other enclosing stages/built-in inputs/cold coverage
and final gates. Strict Stage F reconstruction remains final phase 7.


## Cofactor normal transform: implementation with open real/live gates

`port/native_geometry_skin_cofactor.h` replaces BB58/BB68 through the exact
queued B4C0 boundary, using host four-vertex groups, AVX2 pairs and explicit
final-group state reconstruction. No original setup/body/final group runs on
the native path. Each group loads its twelve matrix quads and four normals
before stores, preserving matrix/normal aliases and feedback. Sequential
matrix reads can wrap LS; normal writes remain bounded physical data.
Arbitrary input masks/shuffles and unused vector/pointer/count lanes remain
supported. The arithmetic retains separately rounded FM subtrahends, FMS
cofactors, FM+two-FMA transforms and exact SPU FRSQEST/FI normalization.

`BUZZ_GEOMETRY_JOB_SKIN_COFACTOR=0/1/2` defaults to 0; mode 2 or JOB=2 compares
all GPR/LS bytes, architectural/control state, PC, queued function and image/
depth/policy/overlay against the complete original data closure. No Stage F
exception or state exclusion is added. Protocol 24 / stage 29 has 30 counters,
with independent one-group/bulk and matrix/normal-alias native/strict counts.
Unsupported input is rejected before mutation and receives no native credit.
The existing flushed first-fallback notification remains active and its
abrupt `_Exit` persistence test passes.

**Strict evidence:** 7,808 full-state/boundary cases = 6,400 random counts 4..4096,
640 alias/wrapped cases (576 actual matrix/normal overlaps),512 arbitrary mask/
shuffle cases and 256 special-FP cases, including signed zeros, denormals,
maximum/extended SPU values. Ten rejected-input cases preserve state and
perform no I/O. Three additional independent geometric checks use the
inverse-transpose of diag(2,3,4), checking expected directions, unchanged
matrices/padding and final pointers. Their 5e-4 semantic tolerance reflects
SPU's approximate reciprocal root; **no tolerance is applied to any strict
state, corpus or live checker**.

AF18 composition passes 768 additional strict cases using actual original
cofactor data versus its native implementation, after both original and
native rigid data, including scratch/normal feedback and full 128 GPR call
transcripts. Directional data remains an explicit model in this harness;
these composition cases do not validate the remaining normalized direction
kernels. All previous 6,600 control cases,384 actual-rigid integration cases,
3 numeric palette cases,3 unused-input early cases and 18 rejects pass unchanged.
Owner/off/foreign/image/null-tail/invalid/post-scope routing and both strict
activation modes pass; the 1,024 outer models and 19,508 real-entry models pass.
Runtime 43 controller cases, 224 configuration-getter cases and the durable
first-fallback test pass. There was no pre-freeze arithmetic/state mismatch.

Frozen `build-vulkan-phase3-skin-cofactor-v1`:
EXE `7b1bd0a45a1be199f781280f6b3ea9441833ed29b97dfb3c34372a1c3a2b83dd`;
map `689d08e6f32bafe68572f0c4b8d050db522c64802a2130595b487aa0a2901334`;
generated C `2e4a7d01690cd757a8079ca98fb3cacc98426f783ae75b6167e5d6e870f97ca2`.
Frozen C and four compiled native header/controller copies are preserved.

All five compatibility configurations pass **19,508 exact jobs /58,195
transfers**, zero input changes and failures, Stage F=2 / leaf 827/no contract
override:

| Compatibility arm | JOB | Older flags | SKIN_COFACTOR | New native/strict/fallback |
|---|---|---|---|---|
| Off |0|0|2|No native pipeline|
| Independent |2|0|1|0/0/0|
| Combined native |1|1|1|0/0/0|
| Combined checked |2|1|1|0/0/0|
| Kernel checked |1|1|2|0/0/0|

Both JOB=2 arms pass 19,508 whole-job state/I/O comparisons. Combined arms retain
1,628 AF18, 6,113 weighted roots including 30 palette aliases, 5,018 basic direction
roots and zero rigid roots, all without fallback. **There are zero new real
cofactor calls**; the original trace records BB58=0. These results prove
compatibility of observed paths, not real-data validation of this kernel.
Nonzero native corpus coverage and >=256 strict live cofactor comparisons
remain **OPEN**. Normalized C14C/C280/B5A0 data remains original work.

Same frozen EXE, fresh private saves, all older flags=1, GEOMETRY=1 / Stage F=1 /
leaf827, without compilation/CPU tests/profiling overlap:

| Functional compatibility run | JOB/SKIN_COFACTOR | Frames /last elapsed ms | New native/strict observed |
|---|---|---|---|
| Checked menu,60s |1/2|2,753 /59,573.692|0/0;gate OPEN|
| Native menu,75s |1/1|3,288 /74,690.806|0/0;gate OPEN|
| Native round,60s after start136.5s |1/1|9,424 /195,974.106|0/0;gate OPEN|

The logs confirm the requested modes, no DIFF/HALT/mismatch and no observed
new cofactor native/strict/first-fallback event. Inspected 1280x720 readbacks
show normal menu/studio/crowd and the actual green round/host/player/score UI.
**A normal scene with no new data calls does not close the real/live gate.**
No matched FPS/CPU benefit is claimed. Final interpreter/reversed A/B and
normalized worker/process cycle/time/default decisions remain open; repeatable
CPU savings may matter even with flat FPS.

Private evidence uses `phase3/skin-cofactor-v1-*` and
`analysis/vk-scene-p3-skin-cofactor-v1-{checked-menu,menu,round}.*`.
`phase3/skin-cofactor-v1-build.json` and archived proofs bind committed code,
tests/docs, frozen binary/map/C/native sources, source trace and scene data.
All sessions are terminal and source/generator/save/template/fixture pins
remain unchanged. JOB=0 / leaf 827 / all new flags=0 remain unchanged. Phase 3 and the
continuing goal remain active. Next data work is normalized directional
skinning, followed by other enclosing stages/built-in inputs/cold coverage
and final gates. Strict Stage F reconstruction remains mandatory final phase 7.


## Normalized skin directions: three implementations with open real/live gates

`port/native_geometry_skin_normalized.h` replaces C14C/C154/C158 (one stream),
C280/C288/C290 (two) and B5A0/B5A8/B5B0 (three), through exact queued B868.
Host AVX2 prefix groups use FM+two FMAs and the original four-vector packing,
SPU FRSQEST/FI and scale broadcasts. Explicit final-group calculation preserves
each variant's distinct full GPR ABI, raw lookahead and pointer/count lanes.
No original setup, step, body or final group executes on the native path.

The first primary raw/Y/Z vectors and matrix group come from the caller's
GPR caches, including deliberately different cache/LS values. Initial r33 can
differ from nominal r67; subsequent steps use the advanced nominal pointer.
All secondary/tertiary and next primary/matrix inputs are read before stores.
Primary then secondary then tertiary stores preserve aliases and feedback;
next input/matrix reads can wrap LS while actual writes remain bounded.
Arbitrary incoming masks/shuffles are supported. Preflight occurs before
mutation; unsupported paths return to the original without native credit.

`BUZZ_GEOMETRY_JOB_SKIN_NORMALIZED=0/1/2` defaults to 0; mode 2 or JOB=2 compares
all GPR/LS bytes, architectural/control state, PC, queued function and image/
depth/policy/overlay against the original data closure. No Stage F exception,
register exclusion or strict-check tolerance is added. Protocol 25 / stage 30
has 31 stage counters and per-stream native/strict/alias counters.

**Strict evidence:** 9,219 full-state/boundary cases = 6,144 random across all
three variants, 2,304 targeted alias/wrap cases (1,088 overlaps),768 special-FP
cases and 3 maximum 4,096-vertex cases. Inputs include arbitrary first caches,
different initial/nominal pointers, standard/arbitrary tables, all pointer
biases, signed zeros/denormals and maximum/extended SPU values. Twelve rejects
preserve state and perform no I/O. Nine independent geometric/cache/padding
checks cover diag(2,3,4) normalization at 4/8/64 vertices in each variant,
including first-cache values different from LS and unchanged extra records.
Their 5e-4 geometric tolerance is an additional approximate-root sanity check;
all strict state/corpus/live comparisons remain byte-exact.

AF18 composition adds 1,152 strict actual-normalized cases using three rigid/
cofactor library combinations (original/original,original/native,native/native),
all three normalized targets, formats 5/6, stream selections and scratch
feedback. Original control uses original data; native control consumes the
native normalized implementation. Full 128 GPR call transcripts and all exit
state/LS match. This cohort uses actual rigid/cofactor/normalized data rather
than directional models. Earlier 6,600 control, 384 actual-rigid, 768 actual-cofactor,
3 numeric palette, 3 unused-input early and 18 reject cases remain green.
Owner/off/foreign/image/null-tail/invalid/post-scope routing and both strict
activation modes pass for every stream variant and one-group/bulk/alias shapes.
The 1,024 outer models and 19,508 real-entry models pass, along with runtime 43
controller cases, 232 production-getter cases and abrupt-exit fallback logging.
There was no pre-freeze arithmetic/state mismatch.

Frozen `build-vulkan-phase3-skin-normalized-v1`:
EXE `e6e95de8cd0a3dc5ac3b0e28930e76d153086eeef928cec9ed368a854cfee84a`;
map `3358c4b1d48093c4f8de3c8cdb304b2795b227104b753f30466ad4a3ef388610`;
generated C `ee0849213b7b715f9be2976d5454f00dec7cb9e592ae8bf307b44877961bcacd`.
Frozen C and four compiled native header/controller sources are preserved.

All five compatibility arms pass **19,508 exact jobs /58,195 transfers**,
zero input changes and failures, Stage F=2 / leaf 827/no contract override:

| Compatibility arm | JOB | Older flags | SKIN_NORMALIZED | Native/strict/fallback |
|---|---|---|---|---|
| Off |0|0|2|No native pipeline|
| Independent |2|0|1|0/0/0|
| Combined native |1|1|1|0/0/0|
| Combined checked |2|1|1|0/0/0|
| Kernel checked |1|1|2|0/0/0|

Both JOB=2 arms pass 19,508 whole-job state/I/O comparisons. Combined arms retain
1,628 AF18, 6,113 weighted roots including 30 palette aliases and 5,018 basic direction
roots, with zero fallback. **All three normalized variants have zero real
calls**, matching the original trace. These are compatibility results, not
real validation of an uncalled kernel. Nonzero native corpus coverage and
>=256 strict live checks remain **OPEN separately for all three variants**.

Same frozen EXE, fresh private saves, all older flags=1, GEOMETRY=1 / Stage F=1 /
leaf827, without compilation/CPU tests/profiling overlap:

| Functional compatibility run | JOB/SKIN_NORMALIZED | Frames /last elapsed ms | New native/strict observed |
|---|---|---|---|
| Checked menu,60s |1/2|2,788 /59,780.299|0/0 in all3;gate OPEN|
| Native menu,75s |1/1|3,292 /74,648.948|0/0 in all3;gate OPEN|
| Native round,60s after start136.5s |1/1|9,337 /196,273.486|0/0 in all3;gate OPEN|

Logs confirm requested modes, no DIFF/HALT/mismatch and no observed new
native/strict/first-fallback event. Inspected 1280x720 readbacks show normal
menu/studio/crowd and the actual green round/host/player/score UI. The absence
of calls leaves the new data gates OPEN; normal rendering cannot close them.
No matched FPS/CPU benefit is claimed. Final interpreter/reversed A/B and
normalized worker/process cycle/time/default decisions remain open; repeatable
CPU savings may matter even with flat FPS.

Private evidence uses `phase3/skin-normalized-v1-*` and
`analysis/vk-scene-p3-skin-normalized-v1-{checked-menu,menu,round}.*`.
`phase3/skin-normalized-v1-build.json` and archived proofs bind committed
sources/tests/docs, frozen binary/map/C/native snapshots, source trace and
scene data. All sessions are terminal; source/generator/save/template/fixture
pins remain unchanged. JOB=0 / leaf 827 / all new flags=0 remain unchanged. All AF18
data families now have native implementations, with cold coverage and final
gates still open. Phase 3 and the continuing goal remain active: remaining
index/transform/ring/conversion stages, built-in inputs and final gates still
require work. Strict Stage F reconstruction remains mandatory final phase 7.


## Complete index delta reconstruction (7730/7770)

`port/native_geometry_index_deltas.h` replaces the entire 7730 setup and 7770
loop through the exact queued 77F8 continuation. Host vectors retain eight
independent modulo-u16 prefix chains, four cached input quads and carry across
groups. Every next input is read before all ordered stores. Initial zero
writes, the repeated first destination, final flush, extra 64-byte lookahead,
biased/separate bases and wrapped reads preserve the original alias behavior.
All changed GPRs, including nonpreferred count lanes and final lookahead,
are materialized once. No original setup, step or final iteration executes.
This completes the delta region; the other index regions remain in the
[explicit inventory](native-geometry-index-audit.md#native-implementation-inventory).

`BUZZ_GEOMETRY_JOB_INDEX_DELTAS=0/1/2` defaults to 0. Mode 2 or JOB=2 compares
all 128 GPRs / all LS, architectural/channel/control state, PC/FN and image/depth/
policy/overlay against both original blocks. There is no tolerance, exclusion
or new Stage F exception. Unsupported states fall back before mutation with
no native credit. Ownership/image guards prevent foreign execution. Stage 31,
32 counters and routing protocol 26 add one-group/bulk/empty and alias counts.

Strict tests pass **5,763 full-state/boundary cases**, including 4,096 randomized
entries, 1,408 targeted base/alias cases (828 overlapping writers), 256 wrapped
lookahead cases and 3 reconstructions of one pinned real entry under different
control settings. Counts cover 0/1/31/32/33 and other block boundaries through
65,535; GPR upper lanes and all u16 input/bias bits vary. Three independent
numeric checks exercise eight separate chains, and 8 rejects leave state
unchanged. API routing passes 9 native / 6 strict calls and 4 counted fallbacks,
including owner/off/foreign/image/null-tail/invalid/foreign-LS/post-scope and
r0=77F8 collision. The 1,024 outer models and 19,508 real-entry models, 43 runtime
controller cases, 240 production-getter cases and durable first-fallback test
also pass. There was no pre-freeze arithmetic/state mismatch.

Frozen `build-vulkan-phase3-index-deltas-v1`:
EXE `94ef25cb1bb11c06ae092838ae88039eed76e4d74cb4ffa87037c35de8ac1acc`;
map `100e36fd32ed0e8e99664f07a568b1907c4cbe106d079b06056fc1a371db66f0`;
generated C `03e2f205c7906c79c88b63610b583e6e06e38acbd5898de73c967ba3a3e8c395`.
Frozen C and four compiled native/header/controller copies are preserved.

All five arms pass **19,508 exact jobs / 58,195 transfers**, zero input changes
and failures, Stage F=2 / leaf 827/no contract override:

| Arm | JOB | Older flags | INDEX_DELTAS | Delta native/strict/fallback |
|---|---|---|---|---|
| Off |0|0|2|No native pipeline|
| Independent |2|0|1|9,763/9,763/0|
| Combined native |1|1|1|9,763/0/0|
| Combined checked |2|1|1|9,763/9,763/0|
| Kernel checked |1|1|2|9,763/9,763/0|

Every enabled arm has 2,999 one-group + 6,764 bulk calls, zero empty/alias calls.
Both JOB=2 arms pass 19,508 whole-job state/I/O checks. Combined arms retain
1,628 AF18, 6,113 weighted and 5,018 basic direction native roots, zero fallback.
Real delta coverage is nonzero; synthetic alias/empty cases are not represented
as additional real calls. The width-zero path bypasses the delta root.

Same frozen EXE, fresh private saves, all older flags=1, GEOMETRY=1 / Stage F=1 / leaf 827, no
compilation/CPU tests/profiling overlap:

| Functional scene | JOB/INDEX_DELTAS | Frames /last elapsed ms | Delta evidence (lower bounds) |
|---|---|---|---|
| Checked menu, 60s |1/2|2,169 /59643.636|strict one-group>=235,000;bulk>=315,000|
| Native menu, 75s |1/1|3,288 /74518.179|native one-group>=1,390,000;bulk>=1,895,000|
| Native round, 60s after navigation |1/1|9,396 /196050.902|native one-group>=2,490,000;bulk>=4,920,000|

No differences, halts or first delta fallback were observed. Forced shutdown
leaves periodic live counts as lower bounds, not final fallback totals.
Inspected 1280x720 readbacks show normal menu/studio/crowd and actual green-round
host/player/score UI. Final interpreter/reversed A/B and normalized worker/
process cycles/time/default gates remain OPEN; no FPS/CPU benefit is claimed.
Repeatable CPU savings with flat FPS may justify enabling after all gates.
JOB=0 / leaf 827 / all new flags=0 remain unchanged. Full index decoder, other enclosing
stages/builtins/cold coverage and phase 3 final gates still require work; the
continuing goal remains active, with strict Stage F reconstruction last.

Private evidence uses `phase3/index-deltas-v1-*` and
`analysis/vk-scene-p3-index-deltas-v1-*`; the finalized private manifest binds
committed sources/docs/tests, frozen binaries/map/C/native snapshots,
original entry pin, prior audit, tests, five replays and three scene outputs.
