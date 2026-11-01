# Native geometry phase 3: implementation and evidence

Status: **in progress**, 2026-11-01. Phase 2 ended at `ef906be`, with default
geometry mask 827 (`0x33B`). The continuing objective in
[native-geometry-plan.md](native-geometry-plan.md) authorizes advancing through
subsequent phases after their gates, with strict Stage F reconstruction last.
The outer driver is native; the complete native data path remains open.
Transform/cull `6608` has a separate [audited contract](native-geometry-transform-audit.md).
Native opt-in implementations cover enclosing control, vertex preparation,
alternate data and result/frame finalization. Alternate real/live gates remain
open; strict Stage F cleanup remains last.

Native payload implementations now exist for all nine descriptor storage
families, plus the primary raw float3 path. Remapped weighted scatter also
has a complete native implementation with strict real-corpus/live coverage.
The four cold typed families still have open real-data/live gates, and
enclosing pipeline bodies keep phase 3 open.
Additional-stream BF8 orchestration is also native opt-in with strict
corpus/live checks. Its `0x7118/7298` run-list data decoder now has a native
opt-in implementation; its separate validation is recorded below.
The custom `0x93F0` dispatcher also has a native implementation through its
exact descriptor tail handoff. A complete standalone standard93F0 controller
now composes raw0 and builtin formats1..5 through production wrappers; cold and
final gates remain open.
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
The full low/high bit-unpack region is now independently native as well;
its source-compatible width-16 behavior and evidence are recorded below.
Complete native AEA0/7390 control, zero-width setup and both publications
now compose the five index data regions through exact root return. Final
acceptance/default gates and other image-41 stages remain open.
Complete native 7A90 stitching and frame restoration reaches queued 2738.
Full `79A8/79C8` command relocation is now independently native through queued
7A90; its overlapping-window contract and evidence are recorded below.
Full `7838/78A8` flag expansion is also native opt-in through its actual
post-write capacity decision, with separate evidence below. Whole-index control and publication now have the separate opt-in implementation
documented below.

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
| `0xAEA0` | Index decoding | Complete native opt-in control and five data regions; cold/final gates remain open |
| `0x6608` | Transform/cull | Native opt-in enclosing control, 6188 setup and 6958/66C8 finalization; alternate data is cold native opt-in; strict Stage F remains phase 7 last |
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
| `0xAEA0`, index stage | Complete native opt-in control and five data regions | Cold coverage and final interpreter/A/B/CPU/default gates |
| `0x6608`, transform stage | Complete native enclosing control, 6188 setup and 6958/66C8 suffix; existing Stage F inner kernels; alternate data native opt-in with real/live gates open | Cold real/live and final gates; strict Stage F remains phase 7 last |
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


## Complete low/high index bit unpack (74F0/7640/7C20/7CD0)

`port/native_geometry_index_unpack.h` replaces the complete five-block region,
including 77E8, through exact 7730 or 77F8 continuation. Native host vectors
retain the compressed-input cache, extraction pipeline, both warmups and
backward output progression. The last block is written three times before
the ordinary -64 steps; all next raw reads precede the ordered output stores.
Low/high store orders differ and remain exact even for overlapping bases.
A native final step materializes all GPR temporaries, counters, cached header
bias and final lookahead. No original setup, step or final iteration executes.

Preflight simulates every write from the actual D120 table and cached r99
selector. It supports nonstandard bounded increments, arbitrary extraction
and shuffle tables, biased/separate bases, aliases and wrapped reads. Physical
writes stay in private data; unsupported inputs fall back before any mutation
with no native credit. Full nonpreferred pointer/count lanes are preserved.
The count derives from the actual truncated packed offset, including large
synthetic width/count combinations, rather than assuming groups+2 blindly.

`BUZZ_GEOMETRY_JOB_INDEX_UNPACK=0/1/2` defaults to 0. Mode 2 or JOB=2 compares
all 128 GPRs/all LS, architectural/channel/control state, PC/FN and image/depth/
policy/overlay against the entire original closure. No exclusion, tolerance
or Stage F waiver is added. Stage 32 / 33 counters / protocol 27 expose low/high
native and strict totals. The earlier delta flag remains independent.

**5,378 strict full-state/boundary cases** pass: 4,096 randomized entries,
768 targeted alias/increment-table cases, 256 wrapped lookahead cases, two
pinned real low/high entry snapshots and 256 independent packed-value cases.
Counts include 0/1/31/32/33 through 65,535; widths 1..16, arbitrary GPR upper
lanes, cache/table controls and all byte alignments are covered. The extra
numeric checks independently pack MSB-first values for widths 1..15 (240 cases)
and prove the pinned width 16 behavior at 16 alignments. Ten rejects leave state
unchanged. API routing passes 18 native / 12 strict and 8 counted fallbacks, both
width paths and both tails, aliases, owner/off/foreign/image/null callbacks,
invalid fields/LS and post-scope calls. Tail-PC collisions are included.
The 1,024 outer models / 19,508 real-entry models, 43 runtime controller cases,
248 production-getter cases and durable first-fallback test also pass.

Strict native/reference state was exact from the first standalone run. The
additional ordinary-u16 assumption failed at synthetic width 16. Investigation
of the zero shifts and byte selectors derived the source-compatible rule in
[the format contract](native-geometry-index-audit.md#width-16-source-compatibility).
The independent check now explicitly proves that rule; the native algorithm
and strict checker were unchanged. Diagnostics remain private. Test boundary
traps also received distinct diagnostics to prevent identical-function folding;
production PC/FN checks remain strict. Width 1 and 12..16 remain unobserved in
the real corpus, so synthetic checks are not counted as real calls.

Frozen `build-vulkan-phase3-index-unpack-v1`:
EXE `f8d9eb7bd9b2af94b8835e7858d56b8e2772a5a6f227b75ba4035e91ad401d15`;
map `ee32f33c5bc6bfe01a3f190ab808fe869be7f39b4701bd0db1e136ba6b8b30ef`;
generated C `82492b5c82de931f22a2ec8d511bfe7db213f11aebf3735a2d9bb2f51d495a89`.
Frozen C and four compiled native/header/controller copies are preserved.

All five arms pass 19,508 exact jobs / 58,195 transfers, zero input changes and
failures, Stage F=2 / leaf 827/no contract override:

| Arm | JOB | Older flags | INDEX_UNPACK | Unpack native/strict/fallback |
|---|---|---|---|---|
| Off |0|0|2|No native pipeline|
| Independent |2|0|1|9,763/9,763/0|
| Combined native |1|1|1|9,763/0/0|
| Combined checked |2|1|1|9,763/9,763/0|
| Kernel checked |1|1|2|9,763/9,763/0|

Every enabled arm has 7,211 low + 2,552 high native calls, zero fallback. Both
JOB=2 arms also pass 19,508 whole-job state/I/O checks. Combined arms retain all
9,763 native delta calls, 1,628 AF18, 6,113 weighted and 5,018 basic direction roots,
zero fallback. Thus actual unpack+delta composition is covered by full replay.
Width-zero jobs bypass this root and are not counted as new native work.

Same frozen EXE, fresh private saves, all older flags=1 (including deltas),
GEOMETRY=1 / Stage F=1 / leaf 827, with no compilation/CPU tests/profiling overlap:

| Functional scene | JOB/INDEX_UNPACK | Frames /last elapsed ms | Unpack evidence (lower bounds) |
|---|---|---|---|
| Checked menu, 60s |1/2|2,180 /59588.115|strict low>=545,000;high>=55,000|
| Native menu, 75s |1/1|3,317 /74692.974|native low>=2,985,000;high>=335,000|
| Native round, 60s after navigation |1/1|9,592 /196440.496|native low>=5,825,000;high>=1,560,000|

No differences, halts or first unpack fallback were observed. Live counts
are periodic lower bounds; forced shutdown supplies no final fallback totals.
Inspected 1280x720 readbacks show normal menu/studio/crowd and an actual green
round with host/player/score UI. Final interpreter, reversed same-EXE
A/B and normalized worker/process cycles/time/default gates remain OPEN;
no FPS/CPU benefit is claimed. Repeatable CPU savings with flat FPS can justify
enabling after all correctness/render gates. JOB=0 / leaf 827 / new flags=0 are unchanged.

The [remaining index inventory](native-geometry-index-audit.md#native-implementation-inventory)
keeps 7390 control, full flag expansion, relocation and triangle final ABI
explicit. Phase 3 and the continuing goal remain active; other enclosing stages,
builtins and cold/final gates still require work. Strict Stage F cleanup stays
mandatory last. Private evidence uses `phase3/index-unpack-v1-*` and
`analysis/vk-scene-p3-index-unpack-v1-*`; the finalized manifest binds committed
sources/docs/tests, frozen executable/map/C/native copies, original low/high
fixtures, diagnostics, prior delta proof, tests, five replays and three scenes.


## Complete flag expansion (7838/78A8)

`port/native_geometry_index_expand.h` replaces the entire setup, loop and
final ABI through queued 79A8 or 7DCC. Prepared constant shuffle masks and
host vectors retain the explicit-input cursor, flag offset, popcounts,
sequential carry and selection masks. The first destination repeats after
warmup; all five input/lookahead quads are read before high then low stores.
The native final step reconstructs every changed temporary and full pointer/
counter lane. No original setup, step or final iteration executes. The older
phase-2 bit6 prefix remains independent and unchanged.

The final manager+170 capacity quad is reread after all writes. Output/manager
aliases therefore affect the same signed comparison and selected continuation.
A capacity-error handoff completes this pure data region; the error reporter
runs later on the retained context. The checker never executes its I/O twice.
Preflight bounds all physical writes before mutation, while input reads retain
SPU wrap/floor semantics and biased pointers. Unsupported states fall back
before mutation without native credit.

`BUZZ_GEOMETRY_JOB_INDEX_EXPAND=0/1/2` defaults to 0. Mode 2 or JOB=2 compares
all 128 GPRs/all LS, architectural/channel/control state, PC/FN and image/depth/
policy/overlay against both original blocks, with distinct endpoint identities.
No exclusions, tolerance or Stage F exception are added. Stage 33, 34 counters
and protocol 28 track warmup-only, short, bulk and capacity-exit counts.

**4,691 strict full-state/boundary cases** pass: 4,096 randomized entries,
512 targeted alias/wrap/signed-capacity/pointer-overflow cases, 16 maximum-span
cases (6,337 iterations), one pinned real entry, 64 independent scalar merges and
two independent post-write capacity reversals. Synthetic inputs include
arbitrary GPR upper lanes, extraction/shuffle tables, pointer biases, count 1/
2/3 and bulk, flag/input/output overlap, manager aliasing and LS-wrapped reads.
Both continuations are covered (2,334 synthetic capacity exits); reporting
code is trapped. Seven rejects preserve state.

The 64 scalar checks cover all 16 explicit-data byte alignments, all-zero/all-one
and mixed MSB flags, 128 outputs per case and flag-quad crossings. Flag 1 consumes
the next explicit value; flag 0 emits the next sequential value. The compressed
header remains 16-byte aligned, as required by 7390. An initial numeric fixture
incorrectly biased the header together with the explicit data; correcting
that fixture preserved the native algorithm and strict checker. Its diagnostic
is archived. Arbitrary flag-base states still pass strict source comparisons.
Two additional numeric cases prove capacity changing in both directions after
output overwrites manager metadata; prechecking that limit would be incorrect.

API routing passes 18 native / 12 strict calls and 8 counted fallbacks: warmup/short/
bulk, both exits and strict modes, aliases, tail-PC collisions, owner/off/foreign/
image/null callbacks/invalid fields/foreign LS/post-scope. The 1,024 outer models,
19,508 real-entry models, 43 runtime controller cases, 256 production-getter cases
and durable first-fallback check also pass. There were no strict native/reference
state mismatches before freezing.

Frozen `build-vulkan-phase3-index-expand-v1`:
EXE `2c20ddb10574c78e65b141782ad2de5bdfc20e22615f4e809c123cd97c1324af`;
map `4df645e7e4b15a6f1368137474372d180dcaa969d168fadf6cec41963fc388f5`;
generated C `5ac5ac133676dfb659ec6be11355382e3a9b4473e50b089b972821eaaa5da269`.
Frozen C and four compiled native/header/controller copies are preserved.

All five arms pass 19,508 exact jobs / 58,195 transfers, zero input changes and
failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB | Older flags | INDEX_EXPAND | Expansion native/strict/fallback |
|---|---|---|---|---|
| Off |0|0|2|No native pipeline|
| Independent |2|0|1|19,508/19,508/0|
| Combined native |1|1|1|19,508/0/0|
| Combined checked |2|1|1|19,508/19,508/0|
| Kernel checked |1|1|2|19,508/19,508/0|

Every enabled arm has 7,123 short + 12,385 bulk calls, zero warmup-only/capacity
exits or fallback. Both JOB=2 arms pass 19,508 whole-job state/I/O checks. Combined
arms retain all 9,763 unpack and 9,763 delta roots, 1,628 AF18, 6,113 weighted and
5,018 basic direction roots, zero fallback. Actual unpack+delta+expansion
composition is covered; no synthetic case is represented as a real call.

Same frozen EXE, fresh private saves, all older flags=1 (including unpack and
deltas), GEOMETRY=1 / Stage F=1 / leaf 827, no compilation/CPU tests/profiling overlap:

| Functional scene | JOB/INDEX_EXPAND | Frames / last elapsed ms | Expansion evidence (lower bounds) |
|---|---|---|---|
| Checked menu, 60s |1/2|1,367 / 59,137.446|strict short>=130,000; bulk>=330,000|
| Native menu, 75s |1/1|2,306 / 74,906.059|native short>=970,000; bulk>=2,295,000|
| Native round, 60s after navigation |1/1|6,192 / 196,179.297|native short>=1,575,000; bulk>=5,385,000|

No differences, halts, first expansion fallback or capacity exits were observed.
Periodic live counters are lower bounds; forced shutdown provides no final
fallback totals. Inspected 1280x720 readbacks show normal menu/studio/crowd and
actual green-round host/player/score UI. Final interpreter, reversed same-EXE
A/B and normalized worker/process cycles/time/default gates remain OPEN;
no FPS/CPU benefit is claimed. Repeatable CPU savings can justify enabling
with flat FPS after all correctness/render gates. JOB=0 / leaf 827 / new flags=0 remain.

Full 7390 control, command relocation and triangle final ABI are still open in
the [remaining inventory](native-geometry-index-audit.md#native-implementation-inventory).
Phase 3 and the continuing goal remain active; other enclosing stages/builtins/
cold/final gates still require work. Strict Stage F reconstruction stays last.
Private evidence uses `phase3/index-expand-v1-*` and
`analysis/vk-scene-p3-index-expand-v1-*`; the finalized manifest binds committed
sources/docs/tests, frozen executable/map/C/native copies, original entry pin,
diagnostic, prior unpack proof, tests, five replays and three scenes.


## Complete command relocation (79A8/79C8)

`port/native_geometry_index_relocate.h` replaces both original blocks through
queued 7A90, including every warmup/body/final iteration and the complete
triangle-entry ABI. Host vectors hold the input/output pointers and cached
quads; one invariant alignment shuffle is prepared once. No original setup,
step or final iteration runs. The enclosing controller and 7A90 body remain
separate unfinished work in the [inventory](native-geometry-index-audit.md#native-implementation-inventory).

For C in r87, `ceil(C/16)+1` iterations write one zero warmup quad, repeat the
first destination, then advance by 16. Even C=0 still writes a quad and reads
lookahead. Each cached store precedes both next source reads, retaining
feedback for aliases in either direction. This is deliberately not implemented
as memcpy/memmove. Input reads wrap/floor LS, physical writes remain bounded
at/above E680, and eligibility is decided before any mutation. Oversized or
nonterminating unsigned counts fall back without native credit. Full upper
GPR lanes, arbitrary alignment selectors, biased pointers, post-loop source
cache and final D160/D170/D180 loads are exact.

Own `BUZZ_GEOMETRY_JOB_INDEX_RELOCATE=0/1/2` defaults to zero. Mode 2 or JOB=2
compares all 128 GPRs/all LS, architectural/channel/control state, PC/FN and
image/depth/policy/overlay with the two original blocks. There is no tolerance,
exclusion or new Stage F exception. Stage 34, 35 stage counters and protocol 29
provide per-shape native/strict counts and durable first-fallback reporting.

**5,777 strict full-state/LS cases** pass: 4,096 randomized entries with
arbitrary GPR upper lanes and standard/arbitrary alignment shuffles, 512
targeted overlap/wrap/end-of-LS cases, 16 maximum-span cases, one pinned real
entry, and **1,152 independent byte models**. The numeric models cover all 16
source alignments, biased destination pointers, nine lengths (0/1/15/16/17/
31/32/33/127), separate/identical/forward/backward overlap and wrapped source
windows. They commit the prior cached bytes before fetching from modified LS,
then compare the entire 262,144-byte result. Eight rejects preserve state.
There were no strict native/reference differences or numeric fixture failures.

Real-owner API routing passes 18 native / 12 strict calls and seven counted
fallbacks, including warmup-only/one-group/bulk, both strict modes, aliases,
tail-PC collisions, owner/off/foreign/image/null callback/invalid fields/
foreign LS/post-scope. The 1,024 outer models, 19,508 real-entry models,
43 runtime controller scenarios, **264 production-getter configuration cases**
and durable first-fallback check pass. The frozen production build succeeds.

Frozen `build-vulkan-phase3-index-relocate-v1`:
EXE `b12f13964a9f4c4dba2ee955ebb392468bd030003b8eb0164dc4186becf67712`;
map `7c984a3d693a83acbf5ca9a0b29a4ab726c7d88cc1913912c287cb8326fa2cfc`;
generated C `57efbe355698a1882b17e015769bbf4219caeb23bae7d129840a01ab3c2df166`.
Frozen generated C and four compiled native/header/controller copies are retained.

All five corpus arms pass **19,508 exact jobs / 58,195 transfers**, zero input
changes and failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB | Older flags | INDEX_RELOCATE | Relocation native/strict/fallback |
|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline |
| Independent | 2 | 0 | 1 | 19,508/19,508/0 |
| Combined native | 1 | 1 | 1 | 19,508/0/0 |
| Combined checked | 2 | 1 | 1 | 19,508/19,508/0 |
| Kernel checked | 1 | 1 | 2 | 19,508/19,508/0 |

Every enabled arm has 0 warmup, 9,756 short, 9,752 bulk, zero fallback. Both JOB=2
arms pass all 19,508 whole-job state/I/O checks. Combined arms retain all
9,763 native unpack roots, 9,763 deltas and 19,508 flag expansions, plus 1,628
AF18, 6,113 weighted and 5,018 basic-direction roots, zero fallback. This proves
actual composition of all four completed index data regions; synthetic
coverage is not represented as real native calls.

Functional live runs use the same frozen EXE, fresh private saves, all older
flags=1 including unpack/deltas/expansion, GEOMETRY=1 / Stage F=1 / leaf 827,
background/native mailbox/mute, with no compilation/CPU-test/profiling overlap:

| Scene | JOB/INDEX_RELOCATE | Frames / last elapsed ms | Relocation lower bounds |
|---|---|---|---|
| checked-menu | 1/2 | 1,401 / 59,312.020 | strict: warmup>=0; short>=200,000; bulk>=260,000 |
| menu | 1/1 | 2,252 / 74,979.362 | native: warmup>=0; short>=1,365,000; bulk>=1,785,000 |
| round | 1/1 | 6,062 / 196,219.099 | native: warmup>=0; short>=2,530,000; bulk>=4,375,000 |

Checked menu ran 60s, native menu 75s, and the native round 60s after navigation.
Strict live relocation comparisons reach at least **460,000**. No differences,
halts or first relocation fallback were observed. Periodic counters are lower
bounds; forced shutdown provides no final fallback totals. Inspected 1280x720
native readbacks show normal studio/menu/crowd and actual green-round host/
player/score UI. Cold/unseen shapes remain explicitly identified by zero counts.

Final interpreter, reversed same-frozen-EXE menu/round A/B and normalized
worker/process cycles/time/default gates remain **OPEN**. These functional
scenes make no FPS or CPU benefit claim. Repeatable CPU savings can justify a
later default-on decision with flat FPS after all correctness/render gates.
JOB=0 / leaf 827 / new flags=0 remain. Phase 3 and the continuing goal stay
active; 7390 control, complete triangle/final ABI, other enclosing paths and
cold coverage remain required. Strict Stage F reconstruction stays last.

Private evidence uses `phase3/index-relocate-v1-*` and
`analysis/vk-scene-p3-index-relocate-v1-*`; the finalized manifest binds the
committed sources/docs/tests, source/fixture/save pins, frozen EXE/map/C/native
copies, prior expansion proof, tests, five replays and three scenes.


## Complete triangle stitching and frame restoration (7A90)

`port/native_geometry_index_triangles.h` replaces the entire 7A90 loop and
final frame restoration through queued 2738. Host vectors retain incoming
cached quads, previous output, pointers, populations, remaining count and
selection masks. Prepared constant shuffles and a tagged 32-slot cache avoid
repeated selector preparation for immutable physical LUT quads below E680.
All high-LS LUT accesses are reread/prepared, preserving output/table feedback.
No original setup, step or final iteration runs in the new native region.

The output advance starts with incoming r5, then becomes 48. Eligibility
checks the first 48-byte physical span and all later spans before mutation,
including biased addresses, separate initial/following outputs and u32 sum
wrap. Code/input/LUT reads retain SPU wrap/floor behavior. Zero/oversized
counts and unsafe writes fall back before mutation without native credit.
Initial cached values remain distinct from LS, and arbitrary upper lanes,
alignment/shuffle masks, population masks and LUT bases remain supported.

Every step reads five LUT quads and four next input quads before ordered
stores at +0/+16/+32, then reads both next high-nibble LUT controls AFTER all
three stores. The final native step reconstructs every changed volatile GPR.
After all writes the footer adds 432 to r1, copies the old manager r95 to r3,
forms the aligned publication pointer in r4, then restores r126, r0 and
r80..r103 from the final LS frame in original order. Output/frame aliases
therefore affect restored values. The checker stops before 2738 publication.

`BUZZ_GEOMETRY_JOB_INDEX_TRIANGLES=0/1/2` defaults to zero. Mode 2 or JOB=2
compares all 128 GPRs/all LS, architectural/channel/control state, PC/FN and
image/depth/policy/overlay with the complete original 7A90 body. It uses the
original body directly; the separate phase-2 bit3 wrapper remains the fallback
when the new switch is off or eligibility fails. No exclusions, tolerance or
Stage F waiver are added. Stage 35 / 36 counters / protocol 30 track coverage.
The generator matches exactly the pinned single-line phase-2 wrapper and
preserves its underlying original and legacy native functions.

**4,993 strict full-state/LS cases** pass: 4,096 randomized entries, 768 targeted
input/code/table/frame alias and wrapped-address cases, 16 maximum-span cases
(4,225 iterations), one pinned real entry, **80 independent frame models** and
**32 mutable-LUT proofs**. Random cases include arbitrary initial caches,
upper lanes and control/population masks. Targets include high/low/wrapped/
biased LUT bases, custom separated output spans, code/output feedback,
wrapped stack spills and frame/output overlap. Nine rejects preserve state.
There were no strict native/reference differences or numeric fixture failures.

The independent frame models start from zero source/control bytes, so every
output quad is zero. They apply those writes to a byte model BEFORE loading
all return/nonvolatile spills. Five frame placements and all 16 biases cover
ordinary, wrapped, return-address, r126 and nonvolatile aliases. They verify
all 262,144 LS bytes, r0/r126/r80..r103, every r1/r4 lane and the old manager
value in r3. The mutable-LUT proofs force zero pack outputs over previously
nonzero high tables; both next-prefetch and post-store GPR controls must become
zero. They cover both table aliases at every byte bias and reject stale caching.

Real-owner API routing passes 18 native / 12 strict calls, 12 original-body
checker calls and nine counted fallbacks. Warmup-only/short/bulk, both strict
modes, table/code/frame aliases, tail-PC collisions, off/foreign/image/null
reference/null continuation/invalid fields/foreign LS/post-scope are covered.
Fallback and original-reference functions are separate; checks prove that the
new native mode executes no original body outside strict comparison. The
1,024 outer models, 19,508 real-entry models, 43 runtime controller scenarios,
**272 production-getter cases** and durable first-fallback check pass.

Frozen `build-vulkan-phase3-index-triangles-v1`:
EXE `de456e7b2856d32f8f6c9bbbed5d5bf9992a1053345332dc149949fabb24e5ac`;
map `9d6575a5cd8b61b8a3b83f51384fb73bcb4dfc4f6ccc8bcec34df3e675897f02`;
generated C `36c22c69ea9db3b1f20ede3812e0152ca85f6b938c7b72b77942b3b3c0b6beb8`.
The production build, generated C and four compiled native/header/controller
copies are preserved. The original lift, phase-2 generator and source/save/
fixture pins are unchanged.

All five corpus arms pass **19,508 exact jobs / 58,195 transfers**, zero input
changes and failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB | Older flags | INDEX_TRIANGLES | Triangles native/strict/fallback |
|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline |
| Independent | 2 | 0 | 1 | 19,508/19,508/0 |
| Combined native | 1 | 1 | 1 | 19,508/0/0 |
| Combined checked | 2 | 1 | 1 | 19,508/19,508/0 |
| Kernel checked | 1 | 1 | 2 | 19,508/19,508/0 |

Every enabled arm has 0 warmup, 6,833 short, 12,675 bulk, zero fallback. Both JOB=2
arms pass all 19,508 whole-job state/I/O checks. Combined arms retain all
9,763 unpack roots, 9,763 deltas, 19,508 flag expansions and 19,508 command
relocations, plus 1,628 AF18, 6,113 weighted and 5,018 basic-direction roots,
zero fallback. All five completed index data regions compose on actual inputs.
The enclosing controller, zero-width/setup paths and publication remain
separate unfinished work in the [inventory](native-geometry-index-audit.md#native-implementation-inventory).

Functional live runs use the same frozen EXE, fresh private saves, all older
flags=1 including unpack/deltas/expansion/relocation, GEOMETRY=1 / Stage F=1 /
leaf 827, JOB=1, background/native mailbox/mute. There is no compilation,
CPU-test or profiling overlap:

| Scene | JOB/INDEX_TRIANGLES | Frames / last elapsed ms | Triangle lower bounds |
|---|---|---|---|
| checked-menu | 1/2 | 1,432 / 59,371.571 | strict: warmup>=0; short>=120,000; bulk>=345,000 |
| menu | 1/1 | 2,278 / 75,016.413 | native: warmup>=0; short>=790,000; bulk>=2,345,000 |
| round | 1/1 | 6,085 / 195,889.107 | native: warmup>=0; short>=1,320,000; bulk>=5,765,000 |

Checked menu ran 60s, native menu 75s, and native round 60s after navigation.
Strict live comparisons reach at least **465,000**. No differences, halts or
first triangle fallback were observed. Periodic counters are lower bounds;
forced shutdown provides no final fallback totals. Inspected 1280x720 native
readbacks show normal studio/menu/crowd and actual green-round host/player/
score UI. Zero real/live shape counts remain distinguished from synthetic tests.

Final interpreter, reversed same-frozen-EXE menu/round A/B and normalized
worker/process cycles/time/default gates remain **OPEN**. These functional
scenes make no FPS or CPU benefit claim. Repeatable CPU savings can justify
enabling with flat FPS after all correctness/render gates. JOB=0 / leaf 827 /
new flags=0 remain. Phase 3 and the continuing goal stay active; full index
control/setup/publication, other enclosing paths, builtins and cold coverage
remain required. Strict Stage F reconstruction stays last.

Private evidence uses `phase3/index-triangles-v1-*` and
`analysis/vk-scene-p3-index-triangles-v1-*`; the finalized manifest binds the
committed sources/docs/tests, source/fixture/save pins, frozen EXE/map/C/native
copies, prior relocation proof, tests, five replays and three scenes.


## Complete index control, frames and publication (AEA0/7390)

`port/native_geometry_index_control.h` completes the recognized compressed-index
flow through exact AEA0 root return. Native code owns the empty/raw/decoded
routes, 64-byte outer and 432-byte decoder frames, 7390/73A0 setup, 7428 full
metadata/extraction ABI, 77D8/77E8 zero-width setup, 77F8 expansion setup,
2738 end publication, AEE8 original-pointer publication and AEF8 restoration.
No original control/setup/publication block runs on this path. The selected
five data callbacks retain their independent switches; with all five enabled,
the recognized complete decoder is native, including every data final step.

Read-only eligibility precedes ALL writes. It bounds stack/manager/header
accesses and every actual unpack/delta/expansion/command/triangle output,
including warmups/padding, mutable D120/DA30 output-offset sequences and
source-compatible width-16 behavior. It checks alignment and all three later
capacity frontiers. Protected manager metadata, initial header reads and
return frames cannot be invalidated by admitted writes. Unsupported layouts,
non-triangle counts, width>16, pending interrupts, metadata/frame aliases and
unsafe writes fall back before mutation with no native-control credit.
Input/data feedback remains supported where protected regions are unaffected;
individual data kernels retain their wider standalone alias contracts.

Both publications preserve CWD-selected words and all untouched bytes at
manager+50, including biased stack/manager pointers. The ordinary AF0C call
retains preferred-slot-only LINK(AF10), depth/image/drain semantics and no
entry-PC assignment. Final return checks stack and FN identity as well as PC;
links that name an interior block cannot terminate the checker early. Native
control replaces an already scheduled incoming continuation, including empty
and raw routes. Reporting handlers are never speculatively executed twice.

Own `BUZZ_GEOMETRY_JOB_INDEX_CONTROL=0/1/2` defaults to zero. Mode 2 or JOB=2
compares all 128 GPRs/all LS, architectural/channel/control state and actual
PC/FN/image/depth/policy/overlay against an independently pinned closure of
**all 22 original normal blocks**, with four distinct reporting-frontier traps.
The retained fallback and strict original-reference closure are separate.
No Stage F waiver, exclusion or tolerance is added. Stage 36 / 37 counters /
protocol 31 record empty/raw/zero-width/low-width/high-width coverage.

**5,633 strict full-state/LS/return cases** pass with zero unsupported positive
seeds: 4,096 randomized entries, 512 targeted cases, 768 integrations covering
all **32 child-switch combinations**, **256 independent early/publication byte
models**, and one pinned real AEA0 entry. Cases span all widths 0..16, full
upper GPR lanes, biased stacks/managers, every return depth, interior return-PC
collisions, an incoming scheduled continuation, zero-width/nonzero-delta and
nonzero-width/zero-delta paths, altered extraction selectors, minimum data LS,
empty-frame/manager aliasing and raw arbitrary stream pointers. The byte
models independently apply the four frame spills and selected pointer word,
compare all 262,144 LS bytes and verify stack/link/nonvolatile restoration.

Twenty rejects preserve every context/LS byte and the output plan: foreign LS,
pending interrupt, unsafe manager/stack/header/output spans, metadata/frame
overlap, misalignment, malformed count/width and each capacity frontier.
An added pure-continuation guard initially omitted the normal 77E8 footer;
the guard was corrected before integration. Its diagnostic is preserved.
The native data algorithms and full-state checker were not weakened. A test
build also corrected a helper-name typo before execution. All executed strict
native/reference comparisons were exact before freezing.

Real-owner API routing passes **30 native / 20 strict** calls and 15 counted
fallbacks across five paths, both strict modes and both depth classes. It
covers biased addresses, return-PC collisions, incoming continuations,
separate fallback/reference, off/foreign/image/null table/null reference/all
seven missing child callbacks/invalid fields/pending interrupt/post-scope.
Exactly 20 original-controller calls are strict checks; no original controller
executes in ordinary native mode. The 1,024 outer models, 19,508 real-entry
models, 43 runtime scenarios, **280 production-getter cases** and durable
first-fallback check pass. The production build succeeds.

Frozen `build-vulkan-phase3-index-control-v1`:
EXE `53a91e4591994c3dc8de6c8da35c94853aa0c0f6b7cf464226333635b2579029`;
map `f2e19bb335aa9b00b647c75106b718df775db70881b97fa8616efd9d7b0b91d0`;
generated C `37c3d95b2498573f4bbf30db46c5821e6d84d5d73852b34cecdc14df77d90205`.
Frozen C, the private original closure and four compiled native/header/
controller copies are retained. Original lift, phase-2 generator, saves,
template and fixture pins are unchanged.

All five arms pass **19,508 exact jobs / 58,195 transfers**, zero input changes
and failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB | Older flags | INDEX_CONTROL | Control native/strict/fallback |
|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline |
| Independent control | 2 | 0 | 1 | 19,508/19,508/0 |
| Combined native | 1 | 1 | 1 | 19,508/0/0 |
| Combined checked | 2 | 1 | 1 | 19,508/19,508/0 |
| Kernel checked | 1 | 1 | 2 | 19,508/19,508/0 |

Every enabled arm has 9,745 zero-width + 7,211 low + 2,552 high roots, no empty/
raw real roots and zero control fallback. Both JOB=2 arms pass all 19,508
whole-job state/I/O checks. Independent control retains the prior data paths;
combined arms execute all five new data regions: 9,763 unpack + 9,763 delta +
19,508 expansion + 19,508 relocation + 19,508 triangle roots, zero fallback.
They also retain 1,628 AF18, 6,113 weighted and 5,018 basic-direction roots.
This is actual whole-index native composition on every captured image-41 job.

Functional live runs use the same frozen EXE, JOB=1, all older flags=1,
GEOMETRY=1 / Stage F=1 / leaf 827, fresh private saves, background/native
mailbox/mute, with no compilation/CPU-test/profiling overlap:

| Scene | JOB/INDEX_CONTROL | Frames / last elapsed ms | Control lower bounds |
|---|---|---|---|
| checked-menu | 1/2 | 1,401 / 59,586.336 | strict: empty>=0; raw>=0; zero>=80,000; low>=340,000; high>=35,000 |
| menu | 1/1 | 2,291 / 74,947.213 | native: empty>=0; raw>=0; zero>=575,000; low>=2,370,000; high>=260,000 |
| round | 1/1 | 6,044 / 196,221.646 | native: empty>=0; raw>=0; zero>=1,565,000; low>=4,265,000; high>=1,050,000 |

Checked menu ran 60s, native menu 75s, native round 60s after navigation.
Strict live root comparisons reach at least **455,000**, including >=256
in each zero/low/high cohort. No differences, halts, unexpected continuations/
capacity frontiers or first control fallback were observed. Periodic counts
are lower bounds; forced shutdown provides no final fallback totals. Inspected
1280x720 native readbacks show normal studio/menu/crowd and actual green-round
host/player/score UI. Empty/raw paths and widths 1/12..16 remain distinguished
as synthetic coverage where no real/live input was observed.

Final interpreter, reversed same-frozen-EXE menu/round A/B and normalized
worker/process cycles/time/default gates remain **OPEN**. These functional
scenes make no FPS or CPU benefit claim. Repeatable CPU savings can justify
enabling with flat FPS after all correctness/render gates. JOB=0 / leaf 827 /
new flags=0 remain. Phase 3 and the continuing goal remain active: enclosing
transform/ring/conversion/builtin stages, cold functions and final gates still
require work. Strict Stage F reconstruction stays last.

Private evidence uses `phase3/index-control-v1-*` and
`analysis/vk-scene-p3-index-control-v1-*`. The finalized manifest binds committed
sources/docs/tests, source/fixture/save pins, frozen EXE/map/C/native copies,
private original closure, prior triangle proof, tests, five replays and scenes.


## Transform/cull audit and native finalization (6958/66C8)

The separate [transform contract](native-geometry-transform-audit.md) maps all
18 reachable normal transform/cull blocks, the five-block vertex-preparation
closure, reporting frontier and cold alternate data form. A fresh original-only
trace passes 19,508 exact jobs / 58,195 transfers. Modes 1/2/3 occur 6,970 /
10,467 / 2,071 times; the alternate form and early return are unobserved.
Four independent scalar models match all 128 GPRs and all LS bytes at captured
prologue, active-frame, publication and restoration boundaries (264,192 bytes
each). All 65,536 halfword counts reproduce the source's division by three.
Six portable audit regressions pass. These trace snapshots omit architectural/
channel state and are format evidence, not native acceptance.

`port/native_geometry_transform_finish.h` replaces complete 6958/66C8 result
publication and 480-byte frame return, including native shared 2738 end-pointer
publication. The enclosing 6608 control, 6188 vertex setup and alternate
69E8/6B18 data remain open. Existing Stage F vertex/cull callees retain only
their accepted baseline contract; strict Stage F cleanup remains phase 7 last.

The suffix computes three times the full-vector kept count, inserts the
halfword at manager+180 with CHD(SP,0), conditionally publishes the aligned end
pointer using CWD(SP,8), then restores every nonvolatile vector and the actual
saved return link from LS after both stores. It preserves upper lanes, u32
overflow, biased pointers, selected/padding bytes, wrapped LS reads and
metadata overwriting return-frame data. It replaces an incoming continuation.
Read-only eligibility rejects foreign LS, pending interrupts and unsafe
manager spans before mutation. Unsupported inputs retain the lift without
native credit; wrapped frame reads themselves remain admitted.

`BUZZ_GEOMETRY_JOB_TRANSFORM_FINISH=0/1/2` defaults to zero. Mode 2 or JOB=2
checks all 128 GPRs/all LS, architectural/channel/control state and actual
PC/FN/image/depth/policy/overlay against the pinned original closure. Return
draining checks the loaded link and restored stack, including interior return
PCs and depth-zero indirect returns. No Stage F waiver, tolerance or field
exclusion is used for this new suffix. Stage 37 / 38 counters / protocol 32
record publish and count-only coverage separately.

**4,929 strict full-state/LS/return cases** pass: 4,096 random, 512 targeted,
one real entry and **320 independent publication/frame byte models**. They
cover both publication modes, full upper lanes, every stack/manager byte bias,
metadata/return/nonvolatile aliases, manager limits, LS/u32 wrapped stacks,
count overflow, dynamic loaded links and incoming continuations. Six rejects
preserve every context/LS byte and the plan. The initial standalone drain stub
incorrectly rejected the legitimate indirect-return sentinel from a nested
publication; its harness behavior was corrected to match runtime. The native
algorithm and checker were not weakened; the diagnostic is retained.

Real-owner routing passes **12 native / 8 strict / 8 original-reference** calls
and six counted unsupported fallbacks across both paths, both strict modes
and both depth classes. Off/foreign/image/invalid/post-scope boundaries pass.
The 1,024 outer models, 19,508 real-entry models, 43 runtime scenarios,
**288 production-getter configurations**, durable first-fallback check and
production build pass. No original suffix executes in ordinary native mode.

Frozen `build-vulkan-phase3-transform-finish-v1`:
EXE `596e1e4f3f630c8a56e5d3632350312bd264c29ab1e1097312bb5389b64f49f7`;
map `367e906224d6209d898d5e7a68084d4250d694721901eb41c9bf551be7614f5b`;
generated C `19a3f3c7b395944e4be75bfdf5e37f141e93713df3ce24caddd0fd3557b0ae82`.
Frozen C, original closure and four compiled native/controller/header copies
are retained. Original lift, phase-2 generator, saves and template are unchanged.

All five arms pass **19,508 exact jobs / 58,195 transfers**, zero input changes
and failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB | Older flags | TRANSFORM_FINISH | Native/strict/fallback |
|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline |
| Independent suffix | 2 | 0 | 1 | 19,508/19,508/0 |
| Combined native | 1 | 1 | 1 | 19,508/0/0 |
| Combined checked | 2 | 1 | 1 | 19,508/19,508/0 |
| Kernel checked | 1 | 1 | 2 | 19,508/19,508/0 |

Every real suffix call takes the publish path. Count-only has strict synthetic
coverage, **zero real/live coverage**, and an open real/live gate. Both JOB=2
arms pass all whole-job state/I/O checks. Combined arms retain native index
control and all five index data regions, plus 1,628 AF18, 6,113 weighted-skin
and 5,018 basic-direction roots, with zero respective corpus fallback.

Functional live runs use this same frozen EXE, JOB=1, older flags=1,
GEOMETRY=1 / Stage F=1 / leaf 827, fresh private saves and background/native
mailbox/mute, without compilation, CPU-test or profiling overlap:

| Scene | JOB/TRANSFORM_FINISH | Frames / last elapsed ms | Suffix lower bound |
|---|---|---|---|
| checked-menu | 1/2 | 1,405 / 60,059.808 | strict publication >= 465,000 |
| menu | 1/1 | 2,260 / 74,337.389 | native publication >= 3,180,000 |
| round | 1/1 | 6,232 / 201,546.669 | native publication >= 7,215,000 |

Checked menu ran 60s, native menu 75s, native round 60s after navigation.
At least **465,000 strict live publication comparisons** passed. No
differences, halts, unexpected continuations/capacity errors or first suffix
fallback were observed. Periodic counters are lower bounds; forced shutdown
provides no final fallback totals. Inspected 1280x720 actual-native readbacks
show normal menu/studio/crowd and green-round host/player/score UI.

No FPS/CPU benefit is claimed by these functional runs. Final interpreter,
reversed same-frozen-EXE menu/round A/B, normalized worker/process cycles/time
and default gates remain **OPEN**. Repeatable CPU savings can justify flat-FPS
default-on decisions after correctness/render gates. JOB=0 / leaf 827 / new
flags=0 remain unchanged. Phase 3 and the continuing goal remain active.

Private `phase3/transform-finish-v1-*` evidence binds committed sources/docs,
source/fixture/save pins, the original audit, frozen EXE/map/C/native copies,
original closure, prior index-control proof, tests, five replays and scenes.


## Complete vertex matrix preparation (6188/65EC/65FC)

`port/native_geometry_vertex_prepare.h` replaces every setup operation through
queued `63E0`, including both format thunks. Host vectors form three basis
combinations using Y/Z/X coefficients, compose those and the origin with the
second four-term matrix, and retain the pre-translation partials. The complete
handoff ABI is materialized once, including every upper/broadcast lane, source
pointer, scratch/lookahead pointer, clip bound, scale and path-specific
temporary. The source's SPU RTZ multiply/FMA order, conversions, comparisons
and arbitrary table selectors are preserved. No LS bytes are written.
The [transform contract](native-geometry-transform-audit.md#native-vertex-preparation-618865ec65fc)
documents the fields and all format paths. The enclosing 6608 control and
alternate cull data remain open; the Stage F vertex loop runs after this
boundary and retains only its existing accepted contract.

Own `BUZZ_GEOMETRY_JOB_VERTEX_PREPARE=0/1/2` defaults to zero. Read-only
eligibility rejects foreign LS, pending interrupts or unsafe manager spans
before mutation. Unsupported entries retain the lift without native credit.
Mode 2 or JOB=2 compares all 128 GPRs/all LS, architectural/channel/control
state and actual PC/FN/image/depth/policy/overlay against a pinned private
original 6188/65EC/65FC closure, stopping before `63E0`. No Stage F waiver,
exclusion or tolerance is used. Incoming continuations are replaced, and
the reference must reach both the actual function identity and PC. Stage 38 /
39 counters / protocol 33 distinguish base (0..2), middle (3..4), five (5)
and other (6..255) formats, whose r6 modes are 0/1/2/0 respectively.

**8,513 strict full-state/LS/handoff cases** pass: 4,096 random, all **256
formats x 16 manager byte biases**, 256 special-FP cases, one pinned real entry
and **64 independent exact matrix cases**. They include both manager bounds,
arbitrary full upper lanes/table bytes, incoming continuations and all format
branches. Independent integer-valued identity-matrix geometry checks complete
columns and every Y-column broadcast without tolerance. Six rejects preserve
every context/LS byte and plan. The first strict comparison caught an X/Y/Z
coefficient-order mistake; native code was corrected to source Y/Z/X before
integration and freezing. Its diagnostic is retained. No checker was weakened.
A test-helper float-field spelling was also corrected before its first run.

Real-owner routing passes **24 native / 16 strict / 16 original-reference**
calls and seven counted unsupported fallbacks across four format paths, both
depth classes and both strict modes. Off/foreign/image/null-boundary/invalid/
post-scope and incoming-continuation cases pass. Original preparation runs
only in strict checks or retained fallback, never ordinary native mode.
The 1,024 outer models, 19,508 real-entry models, 43 runtime scenarios,
**296 production-getter cases**, durable first-fallback check and production
build pass. Windows PowerShell 5 initially treated expected test stderr as a
terminating pipeline error; the successful full runner uses PowerShell 7.

Frozen `build-vulkan-phase3-vertex-prepare-v1`:
EXE `8006dfed46e99793ef3d8d1dcff958ae154f250ab2f5be6553326e3666fa2f06`;
map `93377f44406aea675a92d9f329e36e53ae6a7c118d4fafecd93d7602a5d1162d`;
generated C `9770d9870de157dece9ad0c68419d7bf1a8add25e4a89b443f03f05359a7b8d8`.
Frozen C, original setup closure and four compiled native/controller/header
copies are retained. Original lift, phase-2 generator, saves, template and
real-entry fixture remain unchanged.

All five frozen arms pass **19,508 exact jobs / 58,195 transfers**, zero input
changes and failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB | Older flags | VERTEX_PREPARE | Native/strict/fallback |
|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline |
| Independent preparation | 2 | 0 | 1 | 19,508/19,508/0 |
| Combined native | 1 | 1 | 1 | 19,508/0/0 |
| Combined checked | 2 | 1 | 1 | 19,508/19,508/0 |
| Kernel checked | 1 | 1 | 2 | 19,508/19,508/0 |

Every enabled arm has **10,699 base + 8,809 middle** native roots and zero
preparation fallback. Five/other formats have **zero real/live coverage**;
their real/live gates remain open despite full strict synthetic coverage.
Both JOB=2 arms pass all 19,508 whole-job state/I/O checks. Combined arms retain
all 19,508 native transform-finish and index-control roots, the five index
data regions, 1,628 AF18, 6,113 weighted-skin and 5,018 basic-direction roots,
with zero respective corpus fallback. This composes native preparation with
the prior native finalization while retaining the existing Stage F loops.

Functional live runs use the same frozen EXE, JOB=1, older flags=1,
GEOMETRY=1 / Stage F=1 / leaf 827, fresh private saves and background/native
mailbox/mute, with no compilation/CPU-test/profiling overlap:

| Scene | JOB/VERTEX_PREPARE | Frames / last elapsed ms | Preparation lower bounds |
|---|---|---|---|
| checked-menu | 1/2 | 1,389 / 59,687.317 | strict: base>=265,000; middle>=205,000; five>=0; other>=0 |
| menu | 1/1 | 2,236 / 74,482.861 | native: base>=1,780,000; middle>=1,310,000; five>=0; other>=0 |
| round | 1/1 | 5,957 / 196,180.764 | native: base>=3,375,000; middle>=3,465,000; five>=0; other>=0 |

Checked menu ran 60s, native menu 75s, native round 60s after navigation.
At least **470,000 strict live handoffs** pass, including >=256 each in
base/middle. No differences, halts, unexpected continuations/capacity errors
or first preparation fallback were observed. Periodic counts are lower
bounds; forced shutdown provides no final fallback totals. Inspected 1280x720
actual-native readbacks show normal menu/studio/crowd and actual green-round
host/player/score UI. Five/other remain distinguished as cold paths.

No FPS/CPU benefit is claimed. Final interpreter, reversed same-frozen-EXE
menu/round A/B, normalized worker/process cycles/time and default gates remain
**OPEN**. Repeatable CPU savings can justify flat-FPS default-on decisions
after correctness/render gates. JOB=0 / leaf 827 / new flags=0 remain unchanged.
The continuing goal and phase 3 remain active; complete 6608 enclosing flow,
alternate data and other stages still require implementation and validation.
Strict Stage F reconstruction remains the mandatory last phase.

Private `phase3/vertex-prepare-v1-*` evidence binds committed sources/docs,
source/fixture/save pins, frozen EXE/map/C/native copies, original setup closure,
prior transform-finish proof, standalone/routing/configuration tests, five
replays and live scenes.


## Complete enclosing transform control (6608)

`port/native_geometry_transform_control.h` replaces complete enclosing control:
6608/6618 frame/PIC setup, 6744 flag checks, the entire 6790 cull configuration,
66B8 bypass, 69B0 alternate dispatch and 6DFC/6E00/710C/7110 mode thunks.
The independent vertex, normal/alternate cull and finalization callbacks retain
their switches and contracts. With prior preparation/finalization enabled,
the observed normal transform flow composes native control and setup with
the existing Stage F loops and native publication/frame return. Alternate
69E8/6B18 data is **still original**, with implementation/real/live gates open.
This milestone completes enclosing control, not the remaining native inventory.

Read-only eligibility checks owned LS, pending interrupts, manager and full
480-byte-frame spans, manager/frame overlap and the reporting frontier before
any mutation. Unsupported layouts/reporting inputs retain the original flow
once, without native credit. The controller owns only bounded frame/manager
writes; retained data callbacks keep their own input/write contracts. It
preserves full-vector PIC subtraction, original ordered spills, biased pointer
selectors, the full split-multiply/carry division ABI, rounded batches/masks,
attribute-table selection, all constants/prefetches and each mode's temporary
registers. Both bypass paths restore the entire frame; active footer restoration
observes actual late LS contents and the loaded return link.

`BUZZ_GEOMETRY_JOB_TRANSFORM_CONTROL=0/1/2` defaults to zero. Mode 2 or JOB=2
compares all 128 GPRs/all LS, architectural/channel/control state and actual
PC/FN/image/depth/policy/overlay. The private pinned original graph contains
all **12 enclosing blocks**, pure 2738 publication and a reporting trap.
Its vertex and normal/alternate data boundaries deliberately call the **same
public retained data callbacks** as the native controller. The cull bridge
only binds their queued public 6958 to the private original footer; it changes
no state or data algorithm. Thus the existing Stage F volatile-register
exception remains inside those existing callees. The new controller has **no
waiver, exclusion, tolerance or state repair**. Strict Stage F reconstruction
remains phase 7 last. Stage 39 / 40 counters / protocol 34 track seven paths.

**5,505 strict full-state/LS/return cases** pass, with independent full-state
observations at every vertex, cull and footer entry before clobbering/restoration
could mask setup errors. They comprise 4,096 random, 1,152 targeted integrations
(eight child/baseline combinations x nine modes x 16 byte biases), one pinned
real 6608 entry and **256 independent bypass/frame byte models**. Eight
combinations cover preparation/footer off/native and two retained-data model
contracts. Synthetic data models are explicit orchestration tests; the corpus
and live gates below execute actual data callbacks.

Targets include all 192 admissible flag bytes, normal/alternate modes 1/2/3,
idle and both bypasses, all upper
lanes, biased stack/manager, both depth classes, incoming continuations, data
feedback into later metadata and late writes to saved r0. Return links name
6608/6958/66C8 interior PCs. Footer continuation is identified by the actual
restore function, preventing a loaded return PC of 66C8 from restoring twice.
Independent models manually write every spill and selected publication byte,
compare all 262,144 LS bytes and verify full stack/link/nonvolatile restoration
at eight stack placements and all biases. Nine rejects preserve all context/
LS bytes and the plan: foreign LS, pending interrupt, bad manager/frame spans,
manager/frame overlap and reporting input.

Review found callback-PC selection depended on distinct normal/alternate
function addresses. A strengthened routing check first reproduced the bug;
the implementation now chooses PC from the source branch, including shared
callbacks. The test then exposed truncation of a high-bit mode by SPU ILA;
its fixture now stores all 32 bits. Diagnostics and the superseded V1 frozen
build are retained. Neither checker nor expected PC was relaxed. All acceptance
evidence uses the corrected **V2** EXE; no replay used V1.

Real-owner routing passes **42 native / 28 strict / 28 original-controller
references** and 16 counted unsupported fallbacks. Seven paths, both strict
modes/depths, biased pointers, incoming continuations, overwritten interior
return links and shared cull callbacks pass. Off/foreign/image/null reference/
null library/all five missing callbacks/invalid fields/post-scope pass.
The 1,024 outer models and 19,508 real-entry models pass. Runtime code is
unchanged between V1/V2: its 43 scenarios, **304 production-getter cases** and
durable first-fallback check pass. The corrected production build succeeds.

Frozen `build-vulkan-phase3-transform-control-v2`:
EXE `7d6074254221c358130af102a76ddf7c7078a52832bd55060d6e5817762f673e`;
map `bd468f1c7cae34e1bbdea78029cc6d9308d0ead9697d82cd16b9c4d6775caa42`;
generated C `a350a0711a6d5ec252b3619435013a7617b982366d7a52438edbf580663a8fe1`.
Frozen C, the private original controller closure and four compiled native/
controller/header copies are retained. Original lift, phase-2 generator,
save/template and source/fixture pins are unchanged.

All five frozen arms pass **19,508 exact jobs / 58,195 transfers**, zero input
changes and failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB | Older flags | TRANSFORM_CONTROL | Native/strict/fallback |
|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline |
| Independent control | 2 | 0 | 1 | 19,508/19,508/0 |
| Combined native | 1 | 1 | 1 | 19,508/0/0 |
| Combined checked | 2 | 1 | 1 | 19,508/19,508/0 |
| Kernel checked | 1 | 1 | 2 | 19,508/19,508/0 |

Each enabled arm has **6,970 mode-1 + 10,467 mode-2 + 2,071 mode-3** native
roots and zero control fallback. Bypass/idle/alternate have zero observed
real/live coverage; their real/live gates remain open. Both JOB=2 arms pass
all whole-job state/I/O comparisons. Combined arms retain all 19,508 native
preparation/finalization/index-control roots, every prior index data region,
1,628 AF18, 6,113 weighted-skin and 5,018 basic-direction roots, zero respective
corpus fallback. Independent control keeps older child implementations.

Functional live runs use the same frozen V2 EXE, JOB=1, older flags=1,
GEOMETRY=1 / Stage F=1 / leaf 827, fresh private saves and background/native
mailbox/mute, with no compilation/CPU-test/profiling overlap:

| Scene | JOB/TRANSFORM_CONTROL | Frames / last elapsed ms | Control lower bounds |
|---|---|---|---|
| checked-menu | 1/2 | 1,415 / 59,711.999 | strict: one>=35,000; two>=335,000; three>=85,000 |
| menu | 1/1 | 2,288 / 74,671.581 | native: one>=270,000; two>=2,300,000; three>=615,000 |
| round | 1/1 | 6,085 / 196,373.649 | native: one>=640,000; two>=5,070,000; three>=1,250,000 |

Checked menu ran 60s, native menu 75s, native round 60s after navigation.
At least **455,000 strict live controller comparisons** pass, including
>=256 each for modes 1/2/3 with actual native Stage F callbacks. No differences,
halts, unexpected continuations or first control fallback were observed.
Periodic counters are lower bounds; forced shutdown provides no final live
fallback totals. Inspected 1280x720 actual-native menu/studio/crowd and actual
green-round host/player/score readbacks are normal.

No FPS/CPU benefit is claimed. Final interpreter, reversed same-frozen-EXE
menu/round A/B, normalized worker/process cycles/time and default gates remain
**OPEN**. Repeatable CPU savings can justify flat-FPS default-on after all
correctness/render gates. JOB=0 / leaf 827 / new flags=0 remain unchanged.
Phase 3 and the continuing goal remain active: alternate cull data, ring/atomic
flow, complete conversion, builtins/cold coverage and final gates still need
work. Strict Stage F reconstruction remains mandatory last.

Private `phase3/transform-control-v2-*` evidence binds committed sources/docs,
source/fixture/save pins, V2 frozen EXE/map/C/native copies, original controller,
prior vertex-preparation proof, standalone/routing/configuration tests, five
replays and scenes; V1 artifacts remain separately marked as diagnostics.


## Complete alternate cull data (69E8/6B18/6B10): cold opt-in

`port/native_geometry_alternate_cull.h` replaces the entire region: setup,
every iteration including warmup/final drain, and the continuation thunk.
It queues 6958 without executing the footer. **Implementation and synthetic
full-state validation are complete; real/native corpus and >=256 alternate
live-call gates remain OPEN.** No original alternate-data body runs on a
supported native call. This is new strict data work; no Stage F exception
is applied or expanded. Existing Stage F strict reconstruction remains last.

The loop keeps 47 cached pipeline vectors and its setup constants in host
state, then materializes the full final ABI once. Three pairs of vertex
records supply SPU RTZ edge products/differences, winding, clipping, halfword
equality and converted-extent classification. Four rejection bitsets combine
with the tail mask and startup discard mask; population counts advance the
compacted six-byte triangle stream. Every full upper lane remains exact.
All arithmetic, shifts, shuffles, comparisons and conversions preserve source
semantics, including special FP values and u32 overflow.

Read order is part of the contract: alignment/mask lookups, old output and
next index quads precede all three stores; twelve vertex quad reads follow
all stores. The next iteration consumes the original software-prefetch caches.
No input/table/vertex LS cache assumes immutability. Output can overwrite
inputs, tables, constants and vertex records, including low LS and wrapped
addresses, and later reads observe those changes. Every LS operation keeps
the source's u32 wrap and quadword flooring. Setup constants already loaded
into vectors remain unchanged when their LS bytes are overwritten.

Own `BUZZ_GEOMETRY_JOB_ALTERNATE_CULL=0/1/2` defaults to zero. Read-only
eligibility rejects foreign LS, pending interrupts and iteration counts outside
1..8,192 before mutation. The count is `u32(incoming r15 + 2)`, retaining the
two drain iterations and the source's one-iteration wrap case. All legitimate
counts produced from the enclosing u16 index count fit this limit. Unsupported
entries retain the original body without native credit. Mode 2 or JOB=2 checks
all 128 GPRs/all LS, architectural/channel/control state and actual PC/FN/image/
depth/policy/overlay against the three pinned original blocks, stopping at
the actual 6958 function and PC. No tolerance, exclusion or repair is used.
Stage 40 / 41 counters / protocol 35 distinguish warmup/short/bulk.

**5,448 strict full-state/LS/handoff cases** pass: 4,096 random, 512 targeted,
eight maximum-count runs, **288 independent geometric cases**, **288 composed
original-controller/data cases**, and 256 special-FP cases. Targets include
all upper lanes, one/two/bulk/drain counts, LS/u32 wrapping, zero steps, sink
selection and output aliasing index/vertex/mask/alignment/constant data.
The independent geometry model verifies both kept count and exact compacted
index order for 12 triangle counts, modes 1/2/3, both windings, degeneracy,
halfword equality, full/partial clipping and subpixel extents. No tolerance
is used. Composed cases compare original enclosing control plus original data
against native control/data/footer with explicit synthetic vertex records;
the vertex producer is a stated model, not a real Stage F execution claim.
An ordinary captured 6608 LS image supplies only source-pinned constant tables;
it is **not** presented as a captured alternate entry.

Four rejects preserve all context/LS bytes and the output plan. The first
strict run caught swapped extent/table pairings in a classification result;
the native implementation was corrected to the source pairings. The original
checker was not weakened. Initial mismatch/intermediate diagnostics remain
private; all final cases pass before integration and freezing.

Real-owner routing passes **18 native / 12 strict / 12 original references**
and five counted unsupported fallbacks. Warmup/short/bulk, both strict modes/
depths, wrapped output, incoming continuation, off/foreign/image/null-tail/
invalid/post-scope cases pass. The 1,024 outer models, 19,508 real-entry models,
43 runtime scenarios, **312 production-getter cases**, durable first-fallback
check and production build pass. No original alternate body executes in
ordinary eligible native mode; original references belong only to strict checks.

Frozen `build-vulkan-phase3-alternate-cull-v1`:
EXE `a4d2524734fcacfdc295a141792883fd2fb5b91cf4b9d30ef42d1f882d58d237`;
map `c72e96e7297f7bb65c5c66d09f7c8a18fb320b766650a2f7795d1ac3c6796e91`;
generated C `3872ab5e0dbfa7c1e65d94a08470c3b6c1226dd37bc6c993d32c69978283b802`.
Frozen C, raw original oracle and four compiled native/controller/header
copies are retained. Original lift, phase-2 generator, saves, template and
source/table pins remain unchanged.

All five compatibility arms pass **19,508 exact jobs / 58,195 transfers**, zero
input changes or failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB | Older flags | ALTERNATE_CULL | New-data native/strict/fallback |
|---|---|---|---|---|
| Off | 0 | 0 | 2 | No native pipeline |
| Independent alternate switch | 2 | 0 | 1 | 0/0/0 |
| Combined native | 1 | 1 | 1 | 0/0/0 |
| Combined checked | 2 | 1 | 1 | 0/0/0 |
| Kernel checked | 1 | 1 | 2 | 0/0/0 |

There are **zero original or native alternate entries** in these captured
jobs. Zero fallback with zero attempts is not positive native coverage.
Both JOB=2 arms still pass every whole-job state/I/O comparison; combined
arms retain all 19,508 native enclosing-transform/preparation/finalization/
index-control roots, every prior index data region and prior skin-data counts,
with zero respective corpus fallback. This validates integration compatibility.

Functional scenes use the same frozen EXE, older flags=1, GEOMETRY=1 /
Stage F=1 / leaf 827, fresh private saves and background/native mailbox/mute,
without compilation/CPU-test/profiling overlap. Checked menu additionally
uses **JOB=2 with actual native Stage F** to verify whole-job state and I/O:

| Scene | JOB/ALTERNATE_CULL | Frames / last elapsed ms | Observed lower bounds |
|---|---|---|---|
| checked-menu | 2/2 | 1,233 / 58,038.423 | alternate native/strict 0/0; whole-job strict >= 45,000 |
| menu | 1/1 | 2,330 / 75,057.988 | alternate native/strict 0/0; whole-job strict >= 0 |
| round | 1/1 | 6,089 / 196,076.074 | alternate native/strict 0/0; whole-job strict >= 0 |

Checked menu ran 60s, native menu 75s, native round 60s after navigation.
At least **45,000 strict whole-job state/I/O comparisons** pass with native
Stage F enabled. The new alternate function remains uncalled in all scenes;
its real and >=256 live-call gates remain OPEN. No differences, halts,
mismatches, unexpected continuations or first alternate fallback were observed.
Periodic counters are lower bounds; forced shutdown provides no final fallback
totals. Inspected 1280x720 native menu/studio/crowd and actual green-round
host/player/score readbacks are normal. These are compatibility/render checks,
not actual alternate-data rendering evidence.

No FPS/CPU benefit is claimed. JOB=0 / leaf 827 / new flags=0 remain unchanged.
Cold real/live, final interpreter, reversed same-frozen-EXE menu/round A/B,
normalized worker/process cycles/time and default gates remain OPEN.
Repeatable CPU savings can justify flat-FPS default-on only after correctness/
render gates. Phase 3 and the continuing goal remain active. Next implementation
is ring/atomic flow 37C0, followed by complete conversion/builtins, remaining
cold coverage and final gates; strict Stage F reconstruction stays last.

Private `phase3/alternate-cull-v1-*` evidence binds committed sources/docs,
source/table/save pins, frozen EXE/map/C/native copies, original oracle,
the prior transform-controller proof, tests, five compatibility replays and
scenes. Its manifest explicitly records zero real alternate calls and open gates.

## Output allocator 37C0: source and original-trace audit

The [allocator contract](native-geometry-ring-audit.md) now inventories 39
blocks, reporting frontiers, channel sites, computed interrupt helpers and
intra-block atomic/decrementer loops. The existing pinned original replay
still supplies 19,508 exact jobs / 58,195 transfers. The audit validates
7,201 allocator roots/returns and 14,422 ordered ring transfers: 7,201 paired
GETLLAR/PUTLLUC reservations/publications and 20 cursor GETs. Seven audit
tests pass, including wrong publication EA, cross-job refresh, reordered
reservation and failed replay rejection. No new game or replay was run.

Two implementation details are explicit: direct-address allocation uses raw
requested bytes, while lane/shared allocation rounds to 128; the cold
interrupt-enable helper targets unregistered `3CF4`, not its unreachable
textual `3B28` tail. That resolver/interpreter boundary remains to be audited.
The current I/O-root checker works only in a JOB=2 shadow; independent checked
mode must not claim checks that did not run. Native allocator code and every
native/live/render/performance/default gate remain OPEN. No production code,
defaults, frozen builds or saves changed. Phase 3 and the continuing objective
remain active; strict Stage F reconstruction remains the final phase.

## Output allocator 37C0: native kernel, production integration pending

The native host-cache kernel now implements direct allocation, lane wrap/
refresh/publication, both shared atomic retry loops, post-fallback polling,
decrementer delay and exact frame/result return. Both original reporting
continuations are queued with exact entry state; allocation is never restarted
after external effects. Enabled/saved interrupts and foreign contexts are
rejected before writes. The `3CF4` captured instruction is a branch to `3B28`,
but its unregistered resolver boundary remains unsupported by the new reader.

**5,821 strict state/ordered-I/O cases and seven unchanged rejects pass.** This
includes 768 independent result models, 4,096 alias/wrap cases, zero/overflowing
requests, long conditional-atomic retries, 2,400-read delays and both exact
reporting-entry handoffs. One real allocator fixture additionally matches all
captured exit GPR/LS/PC bytes. The unchanged original tracer produced it from
one captured menu job, replayed 1 exact / 6 transfers / zero changes/failures.
Snapshot controls are modeled, not captured; this is not full native corpus
or live evidence. See the [kernel contract](native-geometry-ring-audit.md#native-allocation-kernel-and-standalone-evidence).

No production routing, runtime switch or ABI is added yet; no new game build,
default or save changed. Next is the driver adapter, switch, strict I/O-root
integration and full-corpus/live/render validation. Retained reporting handoffs
must not earn full native-success credit. Final interpreter/reversed A/B/CPU/
default gates, phase 3 and the continuing objective remain OPEN. Strict Stage F
reconstruction stays last.

## Output allocator 37C0: production integration

The public 37C0 wrapper now routes to the native kernel only for its owning
image-41 pipeline after entry/library admission. Own
`BUZZ_GEOMETRY_JOB_RING=0/1` defaults to **0**; 2 is invalid. JOB=2 / RING=1
compares the complete allocator root and its recorded I/O; JOB=1 does not
claim a strict check. ABI 36 and 42 stage counters include ring stage 41.
Both reporting handoffs queue the original continuation with exact state,
without restarting allocation or earning a complete native return credit.
Enabled/saved interrupts still fall back before mutation. See the
[production contract](native-geometry-ring-audit.md#production-switch-and-strict-root-boundary).

The shared production reference drainer is also exercised on a fresh recorded
slice by all **5,821** standalone cases, including both reporting boundaries.
All GPR/LS/architectural/channel/control/PC/FN state and image/depth/policy/
overlay agree; neither shadow repeats external commands. Seven reader rejects
remain unchanged. Driver routing covers 16 native direct calls, 10 counted
unsupported fallbacks, disabled/foreign/post-scope handling and no false strict
credit when its test journal is absent. The full driver test passes 1,024
modeled pipelines and 19,508 real entry models. Runtime tests include 320
configuration cases and durable first-fallback output; 23 I/O-root controller
cases and the production build pass. No new checker exception or Stage F waiver.

Frozen `build-vulkan-phase3-ring-v1`:
EXE `afb728bbb371ad5ccdc5616abb4657242bffde8f545cd6613beb9dc916322531`;
map `8ef5ccaff28902bdba037ac556ba0af144317efb6544c6cda3d02c1f4df5a694`;
generated C `0e54667af3a0c81c4ca77804a487ca06f01489f9b3854b7cbf4fc2748a0878e6`.

All six replay arms pass **19,508 exact jobs / 58,195 transfers**, zero input
changes or failures, Stage F=2 / leaf 827 / no contract override:

| Arm | JOB/RING | Native ring returns | Strict ring roots | Strict whole jobs |
|---|---|---|---|---|
| Driver off | 0/1 | 0 | 0 | 0 |
| Clean independent ring, all older flags 0 | 2/1 | 7,201 | 7,201 | 19,508 |
| Combined native | 1/1 | 7,201 | 0 | 0 |
| Combined checked | 2/1 | 7,201 | 7,201 | 19,508 |
| Combined with original ring | 2/0 | 0 | 0 | 19,508 |
| Additional independent run, older alternate flag 1 (zero calls) | 2/1 | 7,201 | 7,201 | 19,508 |

Every enabled ring arm has zero fallback and zero retained reporting handoffs.
All earlier enabled stage ledgers match the prior alternate-cull build exactly,
including full transform/index control and prior skin data. The original trace
establishes normal lane allocation for these captured jobs; this is not new real
coverage of disabled/shared/interrupt/reporting cases.

Three functional scenes use the same frozen EXE, all older flags=1, RING=1,
GEOMETRY=1 / actual native Stage F=1 / leaf 827, fresh private saves and native
mailbox/background/mute. No compilation, CPU tests or profiling overlap:

| Scene | JOB | Frames / last elapsed ms | Ring native / strict lower bounds | Whole-job strict lower bound |
|---|---|---|---|---|
| checked-menu | 2 | 1,250 / 59,234.391 | 35,000 / 35,000 | 45,000 |
| menu | 1 | 2,232 / 74,439.778 | 2,570,000 / 0 | 0 |
| round | 1 | 6,055 / 196,161.769 | 4,640,000 / 0 | 0 |

Checked menu runs 60s, native menu 75s, native round 60s after 136.5s
navigation. Both inspected 1280x720 actual-native readbacks are normal: purple
menu/studio/crowd and green round with host/player/score UI. No mismatch, halt,
unexpected continuation/capacity or first ring fallback is observed. Forced
shutdown means periodic counts are lower bounds, not final live fallback totals.
The root counter does not establish separate direct/shared branch live gates.

JOB=0 / leaf 827 / new switches=0 remain unchanged. No FPS/CPU benefit is
claimed. Final interpreter, reversed same-frozen-EXE menu/round A/B, normalized
worker/process cycles/time and default gates stay OPEN, as do cold retained
paths. Flat FPS with repeatable CPU savings remains a valid future default-on
reason after correctness/render gates. Phase 3 and the continuing objective
remain active. Next is complete conversion `54E8`, then builtins 1..5, cold/final
gates, shared image work, optional-phase decisions and strict Stage F LAST.

Private `ring-v1-*` evidence binds source/helper/save/template pins, original
kernel/audit proofs, compiled EXE/map/C/native copies, tests, six replay arms,
three scene logs and native frame QA. Original source, saves and prior frozen
builds remain unchanged.

## Output-conversion audit and callback boundaries

The [conversion inventory](native-geometry-conversion-audit.md) establishes
73 standard non-reporting blocks: seven outer control blocks, eleven built-in
output blocks, nine shared field-control blocks, 37 storage-data blocks, seven
stride blocks and two getter/publication blocks. Computed tables are included;
unreachable decoded table words are excluded. This is preparation for complete
native `54E8`, not a completed conversion implementation.

An unchanged original tracer replays **19,508 exact jobs / 58,195 transfers**,
zero input changes/failures. It observes 7,201 conversion roots and 17,278
field dispatches. Three independent scalar models match complete GPR/LS
snapshots for frame entry/setup/return; a fourth checks pointer publication.
Snapshots do not capture the full architectural/channel state. Built-in
outputs, static/empty returns and reporting remain cold. These observations
are neither native acceptance nor function-time measurements.

The audit also found an unguarded synchronous callback resolver. ABI **37**
guards it and the custom stride/input boundaries before any callback effects.
Retained callbacks execute once and make the paired job unsupported; an
unexpected shadow callback fails before effects. Original full-resolver,
queued-branch and register-tail semantics remain intact. A registered target
alone does not establish an audited callback contract. No state mismatch is
waived, and the existing Stage F exception is unchanged.

Validation passes seven audit/generation tests, **53 runtime controller cases**,
320 production configuration cases, durable first-fallback logging and the
production build. Tests verify unchanged guest operations, full-vector frame
and wrap/alias behavior, single retained callback effects, and rejection of
shadow callbacks before effects in both audit and checked-native modes.

Frozen `build-vulkan-phase3-conversion-callback-v1`:
EXE `24d91279d95036ad41a7f597e645894b58556b96df1b2bf8abdd93451502cf42`;
map `3b06bc5384928abb84817a368b92a98733dd1104179cda3beb6f406952784904`;
generated C `3ea9b73209ac1db9f9d03989e9191c13da46c805f338027075728c5582cec453`.

All three frozen replay arms (driver off, combined native, combined checked)
pass **19,508 exact jobs / 58,195 transfers each**, zero changes/failures.
Every stage ledger exactly matches its corresponding prior ring-build arm.
Checked mode completes 19,508 strict whole jobs and 7,201 strict ring returns;
native mode completes 7,201 native ring returns. No opaque callback occurs in
this corpus, so its new rejection paths are covered by runtime tests rather
than real captured callback execution. Existing source/generator/save/template
and prior frozen-build pins remain unchanged.

No new native conversion, live scene, frame dump, FPS/CPU benefit or default-on
claim is made for this increment. Prior ring live/render evidence remains bound
to its own executable. Conversion's native/corpus/live/render/performance gates
remain OPEN; JOB=0 / leaf 827 / new flags=0 remain unchanged. Next: implement
the complete audited conversion regions, then the separate input built-ins,
remaining phase-3 gates, shared image work and optional-phase decisions, with
strict Stage F reconstruction LAST. The continuing objective stays active.

Private `conversion-callback-v1-*` and `conversion-audit-v1` evidence records
the source commit, frozen artifacts, tests, original snapshots and three replay
arms; raw/derived game artifacts remain outside version control.

## Output-conversion descriptor control: standalone implementation

The [native control kernel](native-geometry-conversion-audit.md#native-descriptor-control-kernel-standalone)
now implements frame setup/restoration, static and empty returns, complete
descriptor dispatch scratch state and ordered cursor publication. Formatter
data and stride remain explicit callees. Nonempty built-ins, custom conversion
callbacks and missing-descriptor reporting reject before writes; so do foreign
LS, pending interrupts, invalid spans and frame/manager aliasing. This is a
standalone increment without production routing or a new switch/ABI.

**4,195 strict comparisons pass**, including all GPR/LS, architectural/channel/
control/PC state, final scheduled function and formatter/stride entry states.
Sixteen callee recipes exercise clobbered volatile vectors, changed metadata
and overwritten saved frame slots. The kernel preserves actual late loads
instead of repairing state or reusing stale entry metadata. Ninety-six
independent static/empty frame/publication models cover every stack bias and
three stack boundaries. Three real-input cases use original data at host depths
0/1/2; eleven admission rejects leave the full context unchanged.

The pinned private oracle includes all 73 standard blocks. The initial test
harness omitted its synchronous stride hook; correcting that hook yields the
passing results without a production-checker or kernel change. Original traces,
generated oracle, test EXE/object, diagnostic and final logs are sealed privately
with source/helper/snapshot/save/template bindings. No compilation or CPU test
overlapped a game run.

No production build, native full-corpus/live/render/performance/default claim
is made for this kernel. The prior callback-build replay evidence remains bound
to its executable. Next is production integration and the remaining shared
formatter/storage/output built-ins, then separate input built-ins and required
final gates. Phase 3 and the continuing native-function objective remain OPEN;
strict Stage F reconstruction remains LAST.

## Complete float32 output storage 46D0

The [storage-2 implementation](native-geometry-conversion-audit.md#complete-float32-output-storage-46d0)
now replaces all five blocks `46D0/46FC/4750/47F0/4808`, including every column,
both scatter arms, the final speculative input/sentinel iteration and full
scratch-register ABI. No final lifted data iteration remains. The exact queued
handoff is `4610`, or `4618` for the original r77 skip; field control remains
outside this data region. Six other storage implementations, shared `4178`,
output built-ins and enclosing conversion integration are still required.

The host-local vectors preserve raw float bits and all input/destination reads
before ordered writes, including aliased buffers, repeated destination quads,
unaligned masked fields and wrapped addresses. Admission scans every write
before mutation, with bounded columns/iterations and protected low LS.
**V2 rejects every enabled/saved/active interrupt context**, including enabled
interrupts without a pending event: the original drainer polls between blocks.
Retained unsupported paths execute once and earn no native-success credit.

Own `BUZZ_GEOMETRY_JOB_FLOAT_OUTPUT=0/1/2` defaults to 0. Mode 2 or JOB=2 checks
all GPR/LS/architectural/channel/control/PC state, scheduled function and image/
depth/policy/overlay against the pinned original five-block graph, before the
field continuation executes. ABI 38 has 43 stage counters and reports native/
strict skip, single-quad, double-quad and mixed roots. The checker and existing
Stage F exception are unchanged. The old phase-2 prefix remains available when
this complete region is disabled.

V2 passes **6,177 strict cases**, including 1,792 alias/wrap cases, 256
independent byte models covering every width 1..16 and byte bias, one real
captured entry, zero-work/zero-limit behavior and nine unchanged rejects.
Production routing covers 24 native calls, 16 strict calls, ten counted
unsupported fallbacks and disabled/foreign/post-scope handling. The complete
driver suite also passes 1,024 modeled pipelines and 19,508 real entry models.
Runtime tests pass 53 controller/328 configuration cases and durable fallback
logging; seven audit tests pass. Those runtime/audit sources did not change
with the V2 header-only admission correction, so their V1 logs are explicitly
reused. Kernel/routing tests, build and production acceptance runs use V2.

Frozen `build-vulkan-phase3-float-output-v2`:
EXE `5e13bda7c10ef81f9d36311151e82b3a4a98fe5051eb4a1761438468c181bd63`;
map `52556569c0ed1d407ae11b0493cfc9e35c6079dca1ad863676fde0b1a39eecee`;
generated C `a1d00fbe62756126ff971464775e25f076f4d58c97434853336ccdea71003b83`.

V1's five exact replay arms and its interrupted functional sequence are retained
as diagnostic history, not final acceptance. The correction strengthens entry
admission; it does not change data arithmetic, add a tolerance, repair state or
expand Stage F's baseline exception. Original source/generator/save/template
and earlier frozen builds remain unchanged.

All **five V2 frozen replay arms** pass 19,508 exact jobs / 58,195 transfers
each, zero concurrent input changes or failures, Stage F=2 / leaf 827 and no
contract override:

| Arm | JOB / FLOAT_OUTPUT | Native float roots | Strict float roots | Strict whole jobs |
|---|---|---|---|---|
| Driver off | 0 / 1 | 0 | 0 | 0 |
| Independent, every older switch 0 | 2 / 1 | 7,201 | 7,201 | 19,508 |
| Combined native | 1 / 1 | 7,201 | 0 | 0 |
| Combined checked | 2 / 1 | 7,201 | 7,201 | 19,508 |
| Combined with original float region | 2 / 0 | 0 | 0 | 19,508 |

Every enabled arm has zero float fallback: 1,156 single-quad roots and 6,045
mixed roots, with zero skip/all-double roots. All older enabled stage ledgers
match the prior callback build exactly. Independent mode has only the new
float ledger, including older alternate-cull/ring switches explicitly off.

Three functional scenes use that same V2 EXE, actual native Stage F=1 / leaf
827, GEOMETRY=1, all older switches=1 / FLOAT_OUTPUT=1, native mailbox,
background/mute and fresh private saves. No compilation, CPU tests or profiling
overlap any game run:

| Scene | JOB | Frames / last elapsed ms | Single-quad native / strict lower bounds | Mixed native / strict lower bounds | Whole-job strict lower bound |
|---|---|---|---|---|---|
| checked-menu | 2 | 1,218 / 59,827.954 | 256 / 256 | 30,000 / 30,000 | 40,000 |
| menu | 1 | 2,292 / 74,361.582 | 245,000 / 0 | 2,345,000 / 0 | 0 |
| round | 1 | 5,981 / 196,304.556 | 500,000 / 0 | 3,930,000 / 0 | 0 |

Checked menu runs 60s, native menu 75s, native round 60s after 136.5s
navigation. Both inspected actual-native 1280x720 readbacks are normal:
purple menu/studio/audience and an actual green question round with host,
player, answers, timer and score UI. No mismatch, halt, unexpected continuation/
capacity or first float fallback is observed. Forced shutdown makes periodic
counts lower bounds, not final live fallback totals. Skip/all-double roots
remain cold; mixed roots exercise both scatter arms.

JOB=0 / leaf 827 / new flags=0 remain unchanged. No FPS/CPU improvement is
claimed. Final interpreter counts, reversed same-frozen-EXE menu/round A/B,
normalized worker/process cycles/time and default-on gates remain OPEN, as
do the required remaining conversion regions. Repeatable CPU savings with
flat FPS remain a valid future default-on reason after correctness/render
gates. Phase 3 and the continuing objective stay active; strict Stage F is LAST.

Private `float-output-v2-*` evidence binds committed source/helpers, original
and private-save template pins, frozen EXE/map/C/native copies, tests, five V2
replay arms, three scene logs and two native frame inspections. Superseded V1
proofs are identified separately. Next is complete binary16 output `4880`, the
other storage data, shared formatter/control integration and built-ins.

## Complete binary16 output storage 4880

The [storage-3 implementation](native-geometry-conversion-audit.md#complete-binary16-output-storage-4880)
replaces all seven blocks `4880/48B0/492C/497C/4988/4A9C/4AB0`, including every
column/iteration, both scatter arms, final sentinel write, fresh post-store
prefetch and exact scratch-register ABI. The queued continuation is `4610`, or
`4618` for r77=0. This completes a second storage implementation; five remain.

All destination quads are read before ordered writes. Fresh inputs are then
fetched from the modified LS and converted, even in the final iteration.
Input/output and destination aliases, full-vector masks and wrapped addresses
retain the original effects. Integer conversion preserves truncation, signs,
tiny-magnitude positive zero and saturation; exceptional encodings follow the
lift's contract. The sign-extended low input-step halfword contributes to the
loop limit. IRQ contexts and unsupported spans/budgets reject before mutation.

Own `BUZZ_GEOMETRY_JOB_HALF_OUTPUT=0/1/2` defaults to **0**. Mode 2 or JOB=2
checks all GPR/LS/architectural/channel/control/PC state, scheduled function and
image/depth/policy/overlay against the pinned seven-block original oracle.
Unsupported entries retain the original path and earn no native-success credit.
ABI 39 has 44 stage counters; stage 43 separates skip/single/double/mixed roots.
The old phase-2 prefix remains available with this complete region disabled.
The float32 encoder and existing Stage F exception are unchanged.

Validation passes **7,215 strict cases**, including 1,792 aliases/wraps, 1,024
mixed/alias combinations, 256 independent byte models, 1/128/256-column and
signed-step edges, zero-work/zero-limit cases, one real captured root and nine
unchanged rejects. An independent scalar model checks **1,048,576 float bit
patterns**: every high halfword, all three retained low mantissa bits and both
discarded-tail extremes. Host-local intermediate elision preserves exact native
final-prefetch values; no compared field is excluded or copied from a reference.

Driver routing passes 25 native calls, 17 strict calls, ten counted unsupported
fallbacks and disabled/foreign/post-scope handling, alongside 1,024 pipeline
models and 19,508 real entry models. Runtime tests pass 53 controller and 336
configuration cases plus durable first-fallback logging; seven audit tests and
the production build pass. The initial routing test caught a new counter being
credited to float32; it now uses its own stage. The reference-drain bound now
covers all five transitions per admitted column, tested at 256 columns. The
initial scalar-sweep count caught duplicated batches. Comparators are unchanged.

Frozen `build-vulkan-phase3-half-output-v1`:
EXE `69b82b87c93086b193152632bbefff0cb65bf2ff35ae7ffbef6ccabfe65f1883`;
map `9e3ea2b1d0d7a9ab8c223f3b716a29bac604877bae060edbbedd6a13970e809a`;
generated C `48eca2a3dd833ad586778f723a34746a70eb8d465a397ed007cd8cb55d99df0b`.

All **five frozen corpus arms** pass 19,508 exact jobs / 58,195 transfers each,
zero concurrent input changes/failures, Stage F=2 / leaf 827 and no contract
override:

| Arm | JOB / HALF_OUTPUT | Native half roots | Strict half roots | Strict whole jobs |
|---|---|---|---|---|
| Driver off | 0 / 1 | 0 | 0 | 0 |
| Independent, every older switch 0 (including FLOAT_OUTPUT) | 2 / 1 | 4,513 | 4,513 | 19,508 |
| Combined native | 1 / 1 | 4,513 | 0 | 0 |
| Combined checked | 2 / 1 | 4,513 | 4,513 | 19,508 |
| Combined with original half region | 2 / 0 | 0 | 0 | 19,508 |

Every enabled arm has zero half fallback: 3,654 single-quad and 859 mixed
roots, zero skip/all-double roots. All older enabled stage ledgers match the
prior float V2 build exactly; independent mode has only the new half ledger.

Three functional scenes use that same frozen EXE, actual-native Stage F=1 /
leaf 827, GEOMETRY=1, all older flags=1 including FLOAT_OUTPUT / HALF_OUTPUT=1,
fresh private saves and native mailbox/background/mute. No compilation, CPU
tests or profiling overlap a game run:

| Scene | JOB | Frames / last elapsed ms | Single-quad native / strict lower bounds | Mixed native / strict lower bounds | Whole-job strict lower bound |
|---|---|---|---|---|---|
| checked-menu | 2 | 1,139 / 59,802.030 | 25,000 / 25,000 | 256 / 256 | 40,000 |
| menu | 1 | 2,274 / 74,585.479 | 1,625,000 / 0 | 20,000 / 0 | 0 |
| round | 1 | 6,051 / 196,373.020 | 2,810,000 / 0 | 330,000 / 0 | 0 |

Checked menu runs 60s, native menu 75s, native round 60s after 136.5s
navigation. Both inspected actual-native 1280x720 readbacks are normal:
purple/blue menu/studio/crowd and green round with host, player, podium and
score UI. No mismatch, halt, unexpected continuation/capacity or first half
fallback is observed. Forced shutdown means periodic lower bounds, not final
live fallback totals. Skip/all-double roots remain cold; mixed roots exercise
both scatter arms.

JOB=0 / leaf 827 / new flags=0 stay unchanged. No FPS/CPU benefit is claimed.
Final interpreter counts, reversed same-frozen-EXE menu/round A/B, normalized
worker/process cycles/time and default-on gates remain OPEN. The whole native
conversion inventory and remaining cold gates are still required. Phase 3 and
the continuing objective stay active; strict Stage F reconstruction remains LAST.

Private `half-output-v1-*` evidence binds committed source/helpers, original
source/generator/save/template pins, frozen EXE/map/C/native copies, test
executables and logs, five replay arms, three scene logs and two native frame
inspections. Raw/derived artifacts remain ignored. Next is complete packed
CMP output `4FDC`, the other four storage regions, shared formatter/control
integration and built-ins.

## Complete packed CMP output storage 4FDC

The [storage-6 encoder](native-geometry-conversion-audit.md#complete-packed-cmp-output-storage-4fdc)
now replaces all five blocks `4FDC/5008/50C8/51C8/51E0`, including all columns,
both scatter arms, final sentinel stores, fresh post-store prefetch and full
scratch-register ABI. The queued continuation is exactly `4610`, or `4618` for
the original r77 skip. Three of seven storage implementations are complete;
four remain, alongside shared formatter/control integration and built-ins.

Conversion preserves SPU FMA/RTZ and extended-range semantics, signed31
saturation, lane-specific shifts and shuffle/merge masks for 11/11/10 fields.
Old destination quads are read before ordered stores; input conversion then
observes the modified LS, including aliases on the last iteration. Native final
prefetch arithmetic supplies every final scratch vector. Full-vector sentinel
decisions, alignment and LS/u32 wrap remain exact. Admission checks all writes
before mutation and rejects IRQ contexts, protected spans and excessive loops.

Own `BUZZ_GEOMETRY_JOB_CMP_OUTPUT=0/1/2` defaults to **0**. Mode 2 or JOB=2
checks all GPR/LS/architectural/channel/control/PC state, queued function and
image/depth/policy/overlay against the private pinned five-block original graph.
Unsupported entries retain original execution and earn no native-success credit.
ABI 40 has 45 stage counters; stage 44 reports skip/single/double/mixed native
and strict roots. The older encoders, CMP input decoder and Stage F exception
remain unchanged; no compared field or FP tolerance is relaxed.

Validation passes **7,471 strict cases**, including 1,792 aliases/wraps, 1,024
mixed/alias combinations, 256 independent byte models, column/signed-step and
zero-work/zero-limit edges, 256 special-FP roots, one real captured root and
nine unchanged rejects. An independent integer model checks **32,772 dyadic
encodings**, with distinct component/record permutations and exact single-
significand FMA numerators, signed31 clamp and 11/11/10 bit placement.

Driver routing passes 25 native calls, 17 strict calls, ten counted unsupported
fallbacks and disabled/foreign/post-scope handling, alongside 1,024 pipeline
models and 19,508 real entry models. Runtime checks pass 53 controller/344
configuration cases and durable fallback logging; seven audit tests and the
production build pass.

Frozen `build-vulkan-phase3-cmp-output-v1`:
EXE `3b1437809e400ae16200c4a08152fb3cbc1152b04f7d89f72f4c0bb99c32570f`;
map `e462a43bcbf87ffdd1c4a6f6bf3a9c22fb8bc57e782269c2d88f9423508ac7a2`;
generated C `ed4159253436ad03bc23c4a9c0548794ee90a840eba432deb2712a1cf3abe37d`.

All **five frozen corpus arms** pass 19,508 exact jobs / 58,195 transfers each,
zero concurrent input changes/failures, Stage F=2 / leaf 827 and no contract
override:

| Arm | JOB / CMP_OUTPUT | Native CMP roots | Strict CMP roots | Strict whole jobs |
|---|---|---|---|---|
| Driver off | 0 / 1 | 0 | 0 | 0 |
| Independent, every older switch 0 (including FLOAT/HALF_OUTPUT) | 2 / 1 | 4,424 | 4,424 | 19,508 |
| Combined native | 1 / 1 | 4,424 | 0 | 0 |
| Combined checked | 2 / 1 | 4,424 | 4,424 | 19,508 |
| Combined with original CMP region | 2 / 0 | 0 | 0 | 19,508 |

Every enabled arm has zero CMP fallback: 3,635 single-quad and 789 mixed
roots, zero skip/all-double roots. All older enabled stage ledgers match the
prior half-output build exactly; independent mode has only the new CMP ledger.

Five functional scene attempts use the same frozen EXE, actual-native Stage F=1 /
leaf 827, GEOMETRY=1 and all older native switches=1, fresh private saves and
native mailbox/background/mute. Compilation, CPU tests and profiling never
overlap a game run:

| Scene / attempt | JOB / CMP_OUTPUT | Frames / last elapsed ms | Single-quad native / strict lower bounds | Mixed native / strict lower bounds | Whole-job strict lower bound |
|---|---|---|---|---|---|
| checked-menu | 2/1 | 1,180 / 58,659.610 | 20,000 / 20,000 | 0 / 0 | 35,000 |
| menu | 1/1 | 2,222 / 74,472.446 | 1,535,000 / 0 | 0 / 0 | 0 |
| round | 1/1 | 6,177 / 201,370.069 | 2,730,000 / 0 | 315,000 / 0 | 0 |
| checked-round | 2/1 | 1,529 / 185,093.778 | 85,000 / 85,000 | 0 / 0 | 175,000 |
| cmp-checked-round | 1/2 | 4,528 / 196,311.515 | 1,285,000 / 1,285,000 | 180,000 / 180,000 | 0 |

The first checked menu supplies full-state CMP and whole-job comparisons.
Native menu runs 75s and native round runs 60s after 141.8s navigation. The
additional JOB=2 round attempt has only single-quad CMP: the full-job checker
slows navigation enough that its scheduled inputs do not establish the actual
round. Its `round_start_s=125.8` is a navigation-script marker, not proof of a
round; it earns no mixed-branch or actual-round coverage. A separate JOB=1 /
CMP_OUTPUT=2 round supplies strict mixed roots without whole-job checking.
No checker field or tolerance changes to obtain that coverage.

Both inspected actual-native 1280x720 readbacks are normal: purple/blue menu,
studio/crowd and readable Polish UI; green actual round with host/player,
Adam score0 and Poprawne0. No mismatch, halt, unexpected continuation/capacity
or first CMP fallback is observed. Forced shutdown gives periodic lower bounds,
not final live fallback totals. Skip/all-double roots remain cold; mixed roots
exercise both scatter arms.

Defaults JOB=0 / leaf827 / new flags=0 stay unchanged. No FPS/CPU benefit is
claimed from these functional runs or replay microtimings. Final interpreter
counts, reversed same-frozen-EXE menu/round A/B, normalized worker/process
cycles/time and default-on gates remain OPEN. Repeatable CPU savings with flat
FPS may justify enabling a function after all correctness/render gates pass.
Phase 3 and the continuing objective remain active; strict Stage F reconstruction
is still the final phase.

Private `cmp-output-v1-*` evidence binds committed sources, original
source/generator/save/template pins, frozen EXE/map/C/native copies, standalone
executables/oracles and logs, five corpus arms, all five scene attempts and two
native frame inspections. Raw and derived artifacts remain ignored. Four
storage payloads remain: SNORM16 `4478` is next, followed by UNORM8/I16/U8,
shared `4178`, existing outer-control production integration and built-ins.

## Complete SNORM16 output storage 4478

The [storage-1
encoder](native-geometry-conversion-audit.md#complete-snorm16-output-storage-4478)
now replaces all five blocks `4478/44A8/4538/4600/4630`, every column and
iteration, both scatter arms and final post-store prefetch/full scratch ABI.
Its exact queued continuation is `4610`, or `4618` for r77=0. Four of seven
storage payloads have complete opt-in implementations; three remain alongside
shared formatter/control integration and built-ins.

The canonical SPU operation is FMA `x * 32767.5 - 0.5`, signed32 conversion
scaled by 2^16 and big-endian high-halfword selection for four components.
General vector parameters and shuffle masks keep their full semantics.
All old destination quads are fetched before ordered stores; fresh input
reads then observe modified LS, including aliases on the final iteration.
Final scratch vectors are computed natively from that prefetch. No lifted
final iteration or reference-state repair supplies them. Full-vector sentinel
selection, nominal-field masks, signed loop limit and address wrap stay exact.

`BUZZ_GEOMETRY_JOB_SNORM_OUTPUT=0/1/2` defaults to **0**. Mode 2 or JOB=2
checks all GPR/LS/architectural/channel/control/PC state, scheduled function and
image/depth/policy/overlay against the private pinned original five-block graph.
Admission rejects IRQ contexts, foreign LS, protected writes and excessive
loops before mutation. Unsupported entries retain original execution without
native-success credit. ABI 41 has 46 counters; stage 45 reports skip/single/
double/mixed native and strict roots. Older encoders and the Stage F exception
are unchanged; no compared field or FP tolerance is relaxed.

Validation passes **7,471 strict cases**: 1,792 alias/wrap, 1,024 mixed/alias,
256 independent byte models, column/signed-step and zero-work edges, 256
exceptional-FP roots, one real captured input and nine unchanged rejects.
An independent integer model checks **131,088 dyadic components**, computing
the exact FMA numerator, explicit RTZ single-significand truncation, scale16
signed32 saturation and high-halfword selection. Component/record permutations
vary independently.

The unchanged original tracer also passes **376 selected real jobs / 1,441
transfers**, zero changes/failures, producing the new root fixture from
`menu-both_job_41_0011`. GPR/LS snapshot SHA256 is
`b919cf806d1b65fe51a32cc0cb25bbdea4fe897b148d84e3d9f571a8bac66958`.
Architectural/channel controls in standalone comparisons are modeled identical
inputs, not captured state. Driver routing passes 25 native/17 strict calls,
ten unsupported fallbacks, 1,024 pipeline models and 19,508 real entry models.
Runtime passes 53 controller/352 configuration cases and durable fallback
logging; seven audit tests and the production build pass.

Frozen `build-vulkan-phase3-snorm-output-v1`:
EXE `a0395f6571f2f0a62b226978bd7fac638996beb33697d202a259e365517b025e`;
map `015e2223836a5677ca2ec9d96d7b62b63e01d86947fe3b38df60ca4e757ac384`;
generated C `a21419d859eac90b8d85fdb50a452418eb1f850adc2c44c4d7428477052f3f6c`.

All five SNORM-output frozen corpus arms pass 19,508 exact jobs / 58,195
transfers each, zero input changes/failures, Stage F=2 / leaf 827 and no
contract override:

| Arm | JOB / SNORM_OUTPUT | Native SNORM roots | Strict SNORM roots | Strict whole jobs |
|---|---|---|---|---|
| Driver off | 0/1 | 0 | 0 | 0 |
| Independent, every older switch 0 including CMP/FLOAT/HALF | 2/1 | 1,104 | 1,104 | 19,508 |
| Combined native | 1/1 | 1,104 | 0 | 0 |
| Combined checked | 2/1 | 1,104 | 1,104 | 19,508 |
| Combined original SNORM | 2/0 | 0 | 0 | 19,508 |

Every enabled arm has 1,104 mixed roots and zero SNORM fallback. Skip,
single-quad and all-double roots remain cold; mixed roots exercise both scatter
arms. All older stage ledgers match the parent CMP build exactly; the
independent arm has only the new SNORM ledger.

Three functional scenes use the same frozen EXE, actual-native Stage F=1,
GEOMETRY=1 / leaf 827 / all older native switches=1 / SNORM_OUTPUT=1, fresh
private saves and mailbox/background/mute. No compilation, CPU tests or
profiling overlaps a game run:

| Scene | JOB | Frames / last elapsed ms | Mixed native / strict lower bounds | Whole-job strict lower bound |
|---|---|---|---|---|
| checked-menu | 2 | 1,132 / 57,367.310 | 256 / 256 | 35,000 |
| menu | 1 | 2,269 / 74,513.394 | 180,000 / 0 | 0 |
| round | 1 | 6,402 / 206,986.843 | 665,000 / 0 | 0 |

Checked menu runs 60s, native menu 75s and native round 60s after 147.1s
navigation. Both inspected actual-native 1280x720 readbacks show normal
menu/studio/crowd and actual green round with host/player and readable UI. No
mismatch, halt, unexpected continuation/capacity or first SNORM fallback is
observed. Forced shutdown means periodic lower bounds, not final live fallback
totals. Cold skip/single/all-double gates remain OPEN.

Defaults JOB=0 / leaf 827 / new flags=0 stay unchanged. No FPS/CPU benefit is
claimed from functional runs or replay microtimings. Final interpreter counts,
reversed same-frozen-EXE menu/round A/B, normalized worker/process cycles/time
and default-on gates remain OPEN. Repeatable CPU savings with flat FPS can
justify enabling a function after all correctness/render gates pass. Phase 3 and
the continuing objective remain active; strict Stage F reconstruction stays
LAST.

Private `snorm-output-v1-*` evidence binds committed sources, original
source/generator/save/template pins, the new real fixture and selected-corpus
manifest, frozen EXE/map/C/native copies, tests/oracles and logs, five replay
arms, three scene logs and two actual-native frame inspections. Raw/derived
artifacts remain ignored. Three storage payloads remain: UNORM8 `4B9C` is next,
followed by I16/U8, shared `4178`, existing outer-control production integration
and built-ins.

## Complete UNORM8 output storage 4B9C

The [storage-4
encoder](native-geometry-conversion-audit.md#complete-unorm8-output-storage-4b9c)
now replaces all five blocks `4B9C/4BC8/4C50/4D18/4D30`: every column and
iteration, both scatter arms, initial conversion and final post-store prefetch/
full scratch ABI. It queues exactly `4610`, or `4618` for r77=0. Five of seven
storage payloads have complete opt-in implementations; I16 and U8 integer,
shared formatter/control integration and built-ins remain required.

Canonical conversion uses SPU FMA `x * 255 + 0.5`, unsigned32 conversion scaled
by 2^24 and big-endian high-byte selection. The real captured field has three
bytes; the canonical shuffle can select four components followed by zero
padding. General vectors/masks and RTZ/extended-range behavior remain exact.
All old destination quads are fetched before ordered stores. Fresh input reads
then observe modified LS even on the final sentinel iteration. The straddled
terminal ABI retains an old second-destination quad and pre-increment input
cursor, unlike SNORM16; every scratch vector is computed or retained natively.
No lifted final iteration or reference-state repair supplies terminal values.

Own `BUZZ_GEOMETRY_JOB_UNORM_OUTPUT=0/1/2` defaults to **0**. Mode 2 or JOB=2
checks all GPR/LS/architectural/channel/control/PC state, queued function and
image/depth/policy/overlay against the pinned five-block original graph.
Admission rejects IRQ contexts, foreign LS, protected writes and excessive
loops before mutation. Unsupported entries retain original execution without
native credit. ABI 42 has 47 counters; stage 46 separates skip/single/double/
mixed roots. Older encoders and the existing Stage F exception are unchanged.

Validation passes **7,471 strict cases**: 1,792 alias/wrap, 1,024 mixed/alias,
256 independent byte models, column/signed-step and zero-work edges, 256
exceptional-FP roots, one real root and nine unchanged rejects. An independent
integer model checks **131,088 dyadic components** with distinct permutations
for each component/record. Its FMA numerator `255*n+1024` on the n/2048 grid
fits the single significand exactly; it independently applies scale24,
unsigned32 saturation and high-byte selection.

The real captured input is `multi-private_job_41_0088`, with sixteen columns
and three-byte fields; snapshot SHA256 is
`cc422028e1cc75bb049cac44b1d38f760e85b17574a53ad7214bacd5c401d8f6`.
Snapshots capture GPR/LS; architectural/channel controls are modeled identical
inputs in standalone comparisons. Driver routing passes 25 native/17 strict
calls, ten counted unsupported fallbacks, 1,024 pipeline models and 19,508 real
entry models. Runtime passes 53 controller/360 configuration cases and durable
fallback logging; seven audit tests and the production build pass. Initial
build diagnostics for stale helper/type names are retained privately; no state
comparison or numerical tolerance was changed to address them.

Frozen `build-vulkan-phase3-unorm-output-v1`:
EXE `5add7f072b7224bb9793c43583f5b7a15db70f6ad75e7c8e38626ce9858fe10b`;
map `231942c604b5bf043f36295116243c0894a13c7d747e3ac5d31ec43d6977ab79`;
generated C `4955154c6afd85fed44cbe5587f3bff18187aff8f60d693cf56b5f6170755c7f`.

All five UNORM-output frozen corpus arms pass 19,508 exact jobs / 58,195
transfers each, zero input changes/failures, Stage F=2 / leaf 827 and no
contract override:

| Arm | JOB / UNORM_OUTPUT | Native UNORM roots | Strict UNORM roots | Strict whole jobs |
|---|---|---|---|---|
| Driver off | 0/1 | 0 | 0 | 0 |
| Independent, every older switch 0 including SNORM/CMP/FLOAT/HALF | 2/1 | 36 | 36 | 19,508 |
| Combined native | 1/1 | 36 | 0 | 0 |
| Combined checked | 2/1 | 36 | 36 | 19,508 |
| Combined original UNORM | 2/0 | 0 | 0 | 19,508 |

Every enabled arm has 36 mixed roots and zero UNORM fallback. Skip, single-quad
and all-double roots remain cold; mixed roots exercise both scatter arms. All
older stage ledgers match the parent SNORM build exactly; the independent arm
has only the new UNORM ledger.

Functional scenes use the same frozen EXE, actual-native Stage F=1 / GEOMETRY=1
/ leaf 827 / all older native switches=1, fresh private saves and
mailbox/background/mute. No compilation, CPU tests or profiling overlaps a game
run:

| Scene | JOB / UNORM_OUTPUT | Frames / last elapsed ms | Mixed native / strict lower bounds | Whole-job strict lower bound |
|---|---|---|---|---|
| checked-menu | 2/1 | 1,170 / 59,804.747 | 1 / 1 | 35,000 |
| menu | 1/1 | 2,286 / 75,058.508 | 10,000 / 0 | 0 |
| round | 1/1 | 6,068 / 195,941.671 | 20,000 / 0 | 0 |
| kernel-checked-menu | 1/2 | 2,247 / 74,559.794 | 10,000 / 10,000 | 0 |

The whole-job checked menu runs 60s, native menu 75s and native round 60s after
136.4s navigation. Whole-job checking slows the menu enough that its UNORM
periodic count remains below 256. A separate 75s JOB=1 / UNORM_OUTPUT=2 menu
therefore supplies at least 10,000 full-state UNORM checks, using the same
checker fields and frozen executable.

Both inspected actual-native 1280x720 readbacks show a normal menu/studio/crowd
and actual green round with host/player and readable UI. No mismatch, halt,
unexpected continuation/capacity or first UNORM fallback is observed. Forced
shutdown means periodic lower bounds, not final live fallback totals.
Skip/single/all-double roots remain cold and their gates remain OPEN.

Defaults JOB=0 / leaf 827 / new flags=0 stay unchanged. No FPS/CPU benefit is
claimed from functional runs or replay microtimings. Final interpreter counts,
reversed same-frozen-EXE menu/round A/B, normalized worker/process cycles/time
and default-on gates remain OPEN. Repeatable CPU savings with flat FPS can
justify enabling a function after all correctness/render gates pass. Phase 3 and
the continuing objective remain active; strict Stage F reconstruction stays
LAST.

Private `unorm-output-v1-*` evidence binds committed sources, original
source/generator/save/template pins, the original real fixture and audit
provenance, frozen EXE/map/C/native copies, tests/oracles and logs, five replay
arms, all four executed scene logs and two actual-native frame inspections.
Raw/derived artifacts remain ignored. I16 `4DD0` and U8 integer `52B4` are the
two remaining storage payloads, followed by shared `4178`, existing
outer-control production integration and built-ins.

## Complete integer output storage 4DD0 and 52B4

The [remaining two storage
encoders](native-geometry-conversion-audit.md#complete-integer-output-storage-4dd0-and-52b4)
now have complete native opt-in implementations, five blocks each. I16 uses
signed scale16/high-halfword conversion without FMA; U8 integer adds +0.5 then
uses unsigned scale24/high-byte conversion without multiplying by 255. All
columns/iterations, both scatter arms, final post-store prefetch and full
scratch ABI are native. Exact queued boundaries are `4610/4618`.

Both preserve full-vector parameters, ordered alias effects, signed limits,
sentinel masks and LS/u32 wrap. Admission rejects IRQ contexts, foreign LS,
protected writes and excessive loops before mutation. No final lifted iteration
or reference-state repair supplies terminal values. I16's column comparison
clobbers r17, preserving r16; the U8 integer scratch ABI also differs from the
normalized encoders and is checked byte-for-byte.

Own `BUZZ_GEOMETRY_JOB_I16_OUTPUT=0/1/2` and
`BUZZ_GEOMETRY_JOB_U8_INTEGER_OUTPUT=0/1/2` both default to **0**. Mode 2 or
JOB=2 compares every GPR/LS/architectural/channel/control/PC field, queued
function and image/depth/policy/overlay to the pinned original five-block graph.
ABI 43 has 49 counters, with stages 47/48 reserved for the new kernels. Input
I16/U8 decoders, older output encoders and the Stage F exception are unchanged.

Each passes **7,503 strict cases**, including 1,792 alias/wrap, 1,024 mixed/alias,
256 independent byte models, column/signed-step and zero-work edges, 256
special-FP roots, 32 boundary roots and nine unchanged rejects. Each also passes
**1,048,576 independent FP-bit components**, covering all high halfwords and 16
low patterns with distinct component/record permutations. I16's model checks
full signed32 result bits via significand/exponent decomposition; U8's model
checks integer-byte boundaries independently of SPU FP helpers.

No real I16/U8 root is captured. Calibration masks/layouts come from real
SNORM/UNORM states but selecting a different function is explicitly synthetic.
An additional **1,152 enclosing-conversion cases** patch one field in the
pinned `54E8` input to storage 5/7, varying four component counts, all 16 offsets,
strides 16/23/48 and host depths 0/1/2. Each datatype executes 576 native strict
roots, including 192 I16 and 144 U8 mixed roots; every root and entire enclosing conversion
matches all GPR/LS/control/PC and final function pointers. Other data and the
controller use the original 73-block graph. These are synthetic function
comparisons, not captured whole-job replay or native-controller acceptance.
The initial coverage assertion used only strides divisible by 16; stride 23
adds mixed-column coverage without changing any state checker or kernel.
A later source-mask review found that descriptor byte 4 controls write width;
the final test varies that explicit byte as well as component metadata. The
fresh 1,152-case run passes, including 192 I16 and 144 U8 mixed roots. Earlier
coverage diagnostics remain private.

Driver routing passes 25 native/17 strict calls and ten unsupported fallbacks
per datatype, plus 1,024 pipeline/19,508 real entry models. Runtime passes 53
controller/376 configuration cases and durable fallback logging. Seven audit
tests and the production build pass. All seven storage payloads now have
native implementations; their individual cold and final gates still apply.

Frozen `build-vulkan-phase3-integer-output-v1`:
EXE `6cd9a8808408795b9078ea9165da24002e647646c8d7b025c6fa431bf04b43fd`;
map `4f6d461f492ee32b0327464606313b9f1d39b4d43a1479b6bd96e7f726a5158d`;
generated C `e1c9953714e16e76368858e11a538d05c7e3831e0f27a4baf9cbe8630b99ef1b`.

Five integer-output compatibility corpus arms pass 19,508 exact jobs / 58,195
transfers each, zero changes/failures, Stage F=2 / leaf 827 and no contract
override:

| Arm | JOB / I16_OUTPUT / U8_INTEGER_OUTPUT | New native / strict roots | Strict whole jobs |
|---|---|---|---|
| Driver off | 0/1/1 | 0 / 0 | 0 |
| Independent, every older switch 0 | 2/1/1 | 0 / 0 | 19,508 |
| Combined native | 1/1/1 | 0 / 0 | 0 |
| Combined checked | 2/1/1 | 0 / 0 | 19,508 |
| Combined original integer data | 2/0/0 | 0 / 0 | 19,508 |

All older stage ledgers match the parent UNORM build exactly. Neither integer
datatype occurs in this real corpus: zero new native roots is compatibility
evidence, not new native-data acceptance. Nonzero new native coverage is the
explicitly synthetic 576 strict enclosing-conversion roots per datatype, 7,503
strict standalone cases and driver routing.

Three functional scenes use that same frozen EXE, actual-native Stage F=1 /
GEOMETRY=1 / leaf 827 / all older flags=1 / both integer output flags=1, fresh
private saves and mailbox/background/mute. No compilation, CPU tests or
profiling overlaps a game run:

| Scene | JOB | Frames / last elapsed ms | I16 new native / strict | U8 integer new native / strict | Whole-job strict lower bound |
|---|---|---|---|---|---|
| checked-menu | 2 | 1,175 / 57,831.936 | 0 / 0 | 0 / 0 | 35,000 |
| menu | 1 | 2,245 / 74,516.652 | 0 / 0 | 0 / 0 | 0 |
| round | 1 | 6,131 / 196,327.168 | 0 / 0 | 0 / 0 | 0 |

Checked menu runs 60s, native menu 75s and native round 60s after 136.5s
navigation. Both inspected actual-native 1280x720 readbacks show normal
menu/studio/crowd and actual green round with host/player and readable UI. No
mismatch, halt, unexpected continuation/capacity or first integer fallback is
observed. Forced shutdown counters are periodic lower bounds, not final fallback
totals.

These scenes exercise the existing native flow with the new switches enabled,
but have zero observed new integer native/checked calls. The per-kernel real
corpus and >=256 live gates
remain OPEN; whole-job comparisons and normal compatibility frames do not
replace them. All seven storage implementations now exist, with their individual
cold and final gates retained. Shared formatter 4178, enclosing-control
production integration and built-ins remain required.

Defaults JOB=0 / leaf 827 / new flags=0 stay unchanged. No FPS/CPU benefit is
claimed. Final interpreter counts, reversed same-frozen-EXE menu/round A/B,
normalized worker/process cycles/time and default-on gates remain OPEN.
Repeatable CPU savings with flat FPS can justify enabling a function only after
its correctness/render gates. Phase 3 and the continuing objective remain
active; strict Stage F reconstruction stays LAST.

Private integer-output-v1 evidence binds committed sources, original
source/generator/save/template and calibration pins, frozen EXE/map/C/five
native copies, both standalone tests and the original 73-block integration
oracle, initial coverage diagnostic, five corpus arms, three scene logs and two
native frame inspections. Raw/derived artifacts remain ignored. Next is the
shared 4178 formatter; the existing standalone 54E8 control kernel still needs
production integration and IRQ admission review.

## Shared formatter 4178 standalone kernel

The [nine-block shared formatter](native-geometry-conversion-audit.md#shared-formatter-4178-standalone-native-kernel)
now has a complete standalone native controller: 128-byte frame, PIC and
alignment preparation, all field/semantic selection, exact data-call frontiers,
late count/frame loads and final return. Data callees are explicit; all seven
complete native encoders are integrated in the synthetic function tests.
No production routing, runtime flag, executable or ABI/default changes occur.

Admission scans layouts without executing lifted code or data arithmetic,
using local GPRs and a cursor-publication overlay. It rejects IRQ contexts,
foreign LS, invalid/control-overlapping spans, data writes into manager or
descriptor metadata, unexpected known jump targets and excessive work before
mutation. Data can alias input and saved-frame storage; the actual ordered
writes and late reloads remain exact. Both successful and rejected admission
leave the complete context unchanged.

Validation passes **1,908 strict GPR/LS/architectural/channel/control/PC/FN
cases**, **2,643 exact child entries** and **2,028 native data roots across all
seven types**. This covers195 seven-family stride/count cases, overlapping
input/output,255 skipped unknown fields,96 independent frame/publication
models,24 return-PC collisions,32 unknown-storage cases,15 unchanged rejects,
all static/empty/field paths and three modeled host depths. Mock callees mutate
late count and saved-frame bytes on both arms; the controller observes their
actual return state. One captured GPR/LS entry from menu-both_job_41_0000 is
reused at host depths0/1/2; this is one data anchor, not a new corpus/live run.
Its SHA256 is `f78601404ee5a584a69a5ea1a74317b387e630789ed3b6f72d03352335d14a8b`.
Architectural controls remain modeled. The private oracle is the pinned original
46-block field graph, including all original data, with no native reference tail.

An initial function-pointer check caught a consumed child continuation surviving
nested `SPU_RET`. The native tail now enters with the queue popped, as the
original drainer does. No compared register, state field or tolerance changed.
Seven audit tests pass. Earlier frontier and build diagnostics remain private.

This is standalone implementation evidence. Existing frozen integer-output
EXE/map/C, original source/generator/save/template pins and defaults are
unchanged. I16/U8 real/live gates stay OPEN. Next is production integration of
this formatter with its own switch, strict original root reference and actual
child-switch admission, then existing54E8 control integration and built-ins.
Real corpus,>=256 live,normal native frames,interpreter,reversed same-frozen-EXE
A/B,normalized worker/process cycles/time and default gates for the new formatter
remain OPEN. No FPS/CPU benefit is claimed; phase3 and the continuing goal stay
active, with strict Stage F reconstruction LAST.

## Shared formatter 4178 production routing

The complete nine-block controller is now production opt-in under
`BUZZ_GEOMETRY_JOB_FORMATTER=0/1/2`, default **0**, with ABI 44 and a separate
stage-49 native/fallback ledger. Owner/image checks, immutable metadata/write
admission and actual used-child switches precede every mutation. Disabled unused
encoders are allowed; a disabled or missing used encoder sends the entire root
to its retained fallback. Its strict reference is the private, pinned original
46-block graph, never native reference data.

Validation on the final source passes:

- 1,908 standalone strict cases / 2,643 exact child entries / 2,028 native data
  calls across all seven types; independent frame models and alias cases retained.
- 117 native / 78 strict production-routing roots and 81 complete data calls;
  28 unchanged owned rejects plus off/foreign/image/post-scope checks.
- 1,024 mock-pipeline scenarios and 19,508 real entry transcripts; 53 runtime
  controller cases, 384 production-getter cases and seven audit tests.

The initial application build rejected C++ syntax in the generated C reference
runner. Explicit C function-pointer types and NULL fixed generation; the final
application, routing and standalone builds pass. That diagnostic is preserved
privately. Native arithmetic and strict comparisons were unchanged.

| Frozen corpus arm | Exact jobs | Transfers | Native formatter | Strict formatter | Formatter fallback |
|---|---:|---:|---:|---:|---:|
| Checked coupled | 19,508 | 58,195 | 7,201 | 7,201 | 0 |
| Off | 19,508 | 58,195 | 0 | 0 | 0 |
| Independent formatter + seven encoders | 19,508 | 58,195 | 7,201 | 7,201 | 0 |
| Native coupled | 19,508 | 58,195 | 7,201 | 0 | 0 |
| Retained formatter, earlier stages enabled | 19,508 | 58,195 | 0 | 0 | 0 |

All arms have zero input divergence and zero failures. The independent arm has
all other newer stages disabled and reproduces the same formatter coverage;
its seven output-encoder ledgers match the parent. Every other comparable stage
ledger matches the integer-output parent. Captured roots all take the known-field
path. Storage presence per formatter root: 1:1,104 / 2:7,201 / 3:4,199 / 4:36 /
5:0 / 6:4,424 / 7:0. These are not total child calls; other callers account for
additional output encoder calls. I16/U8 integer real/live gates stay OPEN.

With native Stage F, live menu logs show at least **30,000 strict formatter
returns** and **35,000 strict whole-job checks**. Separate actual-native menu
and actual-round readbacks (1280x720) look normal. Lower bounds from periodic
logs do not establish final fallback totals. These are functional runs, with
no FPS or CPU benefit claim.

Frozen `build-vulkan-phase3-formatter-v1`:

- EXE SHA256: `a38221e3bffd97eb994e4b8a15562e47deb31cbeb6e0159b73b95b6637784c10`
- MAP SHA256: `4f81154654c7457c51acf97fb82a3cf01843cd02ef6e757b26b4cdb5b0155b12`
- Generated C SHA256: `4737d2d2b0f3a1ab66241bc41c1ceeb7dd48aecca03b47e30941da1724dccc6a`

Private formatter-v1 evidence binds committed sources, prior standalone proof
chain, original source/generator/save/template pins, this frozen EXE/map/C/four
native source copies, unit/routing/runtime tests, original oracle, five corpus
arms, three live scenes and two inspected native readbacks. Raw/derived files
remain ignored. JOB0 / geometry827 / newer switches0 remain unchanged.

Final interpreter, reversed same-frozen menu/round A/B, normalized worker/process
cycles/time and default gates remain OPEN. Flat FPS with repeatable CPU savings
may justify enabling a proven path later. Next is 54E8 production control with
IRQ and child admission review, then built-ins and remaining cold acceptance.
Phase 3 and the continuing goal remain active; strict Stage F reconstruction
stays LAST.

## Native 54E8 descriptor control in production

The seven-block enclosing controller is now production opt-in under
`BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL=0/1/2`, default **0**, with ABI 45 and
stage 50 (51 ledger stages). The complete standard stride getter and ordered
cursor/publication helpers run natively inside this control group. Nonempty
built-in output formats, reporting and arbitrary callbacks remain retained paths;
this milestone does not close their implementation or acceptance gates.

Admission rejects every enabled/saved/active interrupt context. It forecasts the
formatter entry using local GPR arguments and an overlay of the pending cursor
publication, without writing the context, executing lifted code or converting
data. Manager and descriptor metadata must avoid both frames, and all formatter
store destinations/work bounds are checked. Data must also avoid the formatter slot that restores the parent manager pointer,
so subsequent stride-callback admission stays valid. Other frame and
input overlaps remain supported: actual ordered writes and late frame reloads remain authoritative.
Descriptor work requires the actual formatter switch and every used encoder;
static/empty roots need no formatter dependency. All rejected dependencies or
unsupported paths fall back before outer frame/cursor writes.

The strict root reference is the pinned original **73-block** graph with its own
synchronous drainer and computed-target dispatcher. Nested reference calls stay
in that original graph; custom/reporting frontiers fail instead of executing an
unaudited callback. No reference callee uses a new native helper. Comparisons
retain all 128 GPRs, all LS bytes, architectural/channel/control state, PC, queued
function, image/depth/policy/overlay, without exclusions or repair.

A new one-vertex frame-alias regression failed before the saved-manager slot
guard and passes after it. An additional 96 positive cases cover late outer-frame
vectors and the saved formatter result vector; those aliases stay admitted.
V1 positive corpus/live logs are retained as historical
evidence; acceptance below uses the rebuilt V2 executable. No comparison field
was relaxed.

Validation passes:

- **1,853 complete native conversion cases**, including 1,341 native formatter
  calls/all seven encoders, all stack biases, descriptor biases, alias cases and
  return-PC collisions. Forecast and actual formatter admission agree; successful
  admission leaves the full context unchanged. Except three captured GPR/LS entry
  anchors at modeled depths, inputs are explicitly synthetic, with randomized
  GPR vectors and architectural controls. The production C oracle independently
  matches the separate original test oracle on every case.
- **4,096 complete stride cases** across all 256 format selectors, descriptor
  biases/null/wrapped pointers and modeled depths, preserving speculative loads
  and full-vector results. The native helper assumes a standard getter; production
  admission excludes the custom callback before any mutation.
- **4,195 standalone control cases**, 16 identical mock-callee recipes and
  96 independent frame/publication models; the stronger IRQ rule now has
  14 unchanged rejects. The composed reader separately has 21 unchanged rejects.
- **90 native / 60 strict routing roots**, all three paths/seven encoders and
  26 unchanged owned rejects, plus off/foreign/image/post-scope checks. Static and
  empty cases work with the formatter and all encoders disabled; used disabled
  dependencies reject descriptor work. The 1,908-case formatter regression,
  1,024 mock-driver scenarios/19,508 real entry transcripts, 53 runtime controller
  cases, 392 configuration cases and seven audit tests also pass.

Five same-frozen-EXE corpus arms each pass **19,508 exact jobs / 58,195 transfers**
with zero input divergence or failure. Checked and independent arms each have
**7,201 strict native descriptor-control returns**; native mode has 7,201 native
returns, all with zero fallback. The independent arm retains exactly formatter
plus seven encoder dependencies, with all other newer stages off. Their ledgers
match the parent formatter build; all other comparable ledgers also match.
Existing/empty control paths remain absent from real corpus coverage. Cold I16
and U8 integer real/live acceptance remains OPEN.

Live native-Stage-F menu logs reach at least **25,000 strict control returns**
and **30,000 strict whole-job comparisons**. Actual-native menu and actual-round
1280x720 readbacks look normal. The first automated round stopped at round selection
and is not accepted as a round; a retry with visual navigation checkpoints reached
the actual challenge/question stage. Both histories are preserved. Periodic logs
establish lower bounds, not final
fallback totals. These functional runs do not establish FPS or CPU savings.

Frozen `build-vulkan-phase3-conversion-control-v2`:

- EXE SHA256: `d5cf99722e434b5a8df72e502debfdda36430cc78d669a4e702e65127834c9f8`
- MAP SHA256: `6401c5d71104e6673bafe399e5aae1256a130073d298c0b8a6c300e1b45118d9`
- Generated C SHA256: `ff32dfdc15f74c4901ccc579f38d14ca9a1282a6cdf742604678ca32d362f66d`

Private conversion-control-v2 evidence binds this commit's sources, the parent
formatter proofs, original source/generator/save/template pins, frozen EXE/map/C
and five native source copies, both reference oracles, all tests, five corpus
arms, three scenes and two inspected native readbacks. Raw/derived files remain
ignored. Defaults remain JOB0 / geometry827 / new switches0.

Next: native built-in output formats 0..3, then remaining input built-ins and
cold acceptance. Final interpreter counts, reversed same-frozen menu/round A/B,
normalized worker/process cycles/time and default-on gates stay OPEN. Flat FPS
with repeatable CPU savings remains a valid criterion after correctness/render
gates. Phase 3 and the continuing objective remain active; strict Stage F stays LAST.

## Built-in float3 output: standalone kernel

Built-in output format 0 (`56C8/5718`, stride 12) now has a complete standalone
native data kernel in `native_geometry_builtin_float3_output.h`. It packs the
XYZ bits from four float4 rows into three ordered 16-byte stores, then advances
the output by 48 bytes. This covers both original blocks and the final iteration;
no lifted tail, float arithmetic, value repair or reduced comparison is used.
The exact boundary is queued PC/function `5648`, before shared conversion control.

The low 16 bits of entry r10 determine the vertex count. The controller normally
skips zero; a direct zero-count data entry would underflow the original do-loop,
so admission rejects it before mutation. Positive counts execute ceil(N/4)
groups and write **48*ceil(N/4)** bytes, including padding beyond logical N*12.
One 64-byte position group is initially cached. Every iteration loads the next
64 bytes before stores, even the last iteration. Store order is +32, +0, +16;
all LS accesses retain the original quad alignment/wrap and actual LS masks.
The final speculative vectors and full pointer/counter lanes stay exact.

Read-only admission rejects foreign LS, every enabled/saved/active IRQ context,
zero-count entries and any actual padded store into protected LS below E680.
Source and destination may alias; metadata inputs and input loads may wrap.
Successful and rejected admission preserve the entire context. This pure data
reader stops before shared control. Future enclosing-control admission must
also protect its manager/configuration/callback metadata from every padded
store before the first outer-frame or cursor write; a data-region comparison
must never repeat an unaudited stride callback.

Validation passes **6,178 strict GPR/LS/architectural/channel/control/PC/FN
cases**, including **5,154 independent ordered raw-byte models**, 1,024 arbitrary
LS-shuffle-mask cases and 1,024 special FP-bit cases (NaN payloads, infinities,
subnormals and signed zero remain raw bits). It covers biased/aligned pointers,
input/output and manager aliases, wrapped reads, three modeled host depths,
all count remainders and the largest admitted write span: 16,928 vertices from
E680 end exactly at LS size; 16,929 would touch protected wrapped LS and reject.
Eight unsupported cases leave both context and plan unchanged.

**828 full original-controller comparisons** also pass, with 828 strict native
data roots. Only format-0 data is replaced; original control and stride getter
remain on both arms. They include 48 late outer-frame aliases, all count
remainders, biased stacks/output and three depths. These are synthetic function
cases, not captured whole-job replay or a new native enclosing controller.

The private data reference contains the original two blocks and a distinct
frontier trap. A separate pinned 73-block original graph provides the full
conversion reference. Calibration changes output format to 0 in one real
captured 54E8 GPR/LS input, then executes original setup to the data frontier;
it is explicitly **synthetic format 0**, not a captured cold root. The resulting
GPR/LS anchor SHA256 is `60b41abe06e5d21db0532401588c9d75e1afd9f8ae4dc93894f2894c7681e658`.
Seven conversion-audit tests pass. An initial missing initializer-list include
in the new reader was removed by a simple numeric range loop; the first runtime
suite passed without changing arithmetic or comparisons. That build diagnostic
is retained privately.

No production routing, runtime switch, ABI, executable, corpus/live/render or
performance change is claimed. The existing frozen conversion-control-v2 EXE,
map, generated C and native source copies are unchanged. Format-0 production and
real/live acceptance gates remain OPEN, as do formats 1..3 and all final gates.
Next: complete the remaining built-in payloads at 5E28 (format1, stride20),
5760 (format2, stride24) and 5A30 (format3, stride24), then production wrappers,
per-format switches and padding-aware enclosing admission. Phase 3 and the
continuing objective remain active; strict Stage F stays LAST.

## Built-in 20-byte output: standalone kernel

Built-in output format 1 (`5E28/6050/6058`, logical stride 20) now has a complete
standalone native kernel in `native_geometry_builtin20_output.h`. Each pair
contains raw XYZ from semantic 1 and CMP 11/11/10 attributes from semantics 2
and 3. A shared pair-packing routine preserves source operand order for FMA,
RTZ signed31 conversion, lane shifts, both packing shuffles and bit selection.
Host-local vectors hold the pipeline; all initial values that survive into the
final ABI are materialized explicitly. No lifted setup, loop or final tail is
used. The exact frontier is queued PC/function `5648`, before shared control.

The controller rounds the input count to an even value in r29. Admission
requires a positive even low-lane value <=65,536. Zero underflows the do-loop;
odd values cannot reach zero in 40-byte steps. Other counter lanes retain their
actual vector arithmetic. Each pair advances the output by 40 bytes but writes
four ordered quads at +32, +16, +0, +48 after quad alignment. The first prefix
preserves old destination bytes; final padding can include a repeated packed
block beyond logical output. Input/output pointers may be biased or wrap.
Checking only logical count*20 bytes would miss actual writes.

Three input pairs are initially prefetched. Two pairs of CMP results are
prepared ahead; each iteration quantizes the next cached pair before stores.
After all four stores it freshly reads the next destination quad and the next
input pair. That ordering also applies to the last iteration. Only a nonfinal
iteration executes the backedge copies; the final speculative inputs, output
fragment, cached conversions and all 128 guest register vectors remain exact.
Read-only admission checks actual padded destinations and rejects foreign LS,
all enabled/saved/active IRQ contexts, unsupported counts and protected writes.
Every successful and rejected admission preserves the complete context.

Validation passes **5,663 strict GPR/LS/architectural/channel/control/PC/FN
cases**, including 4,096 broad input/output and cross-stream alias cases,
1,024 arbitrary-control cases (including shuffle masks, scale/bias and shifts),
256 special-FP cases, wrapped source/manager reads, biased pointers and three
modeled host depths. **256 independent ordered byte models** combine raw XYZ
with an exact dyadic CMP model, including output/raw-position aliases and fresh
post-store prefetch. **32,772 independent dyadic CMP encodings** cover all 8,193
grid values with distinct component triples and signed saturation. Twelve
rejects preserve both context and plan. At aligned E680, 10,156 rounded vertices
fit the actual padded span exactly; 10,158 would wrap a padded store into
protected LS and reject.

**828 full original-controller comparisons** pass, with 828 native strict data
roots and 48 late outer-frame aliases. Only the format-1 payload is native on
that arm; original controller/getter remain on both sides. This is synthetic
function integration, not real whole-job replay, a newly native enclosing
controller or live acceptance. No arithmetic, checker or tolerance change was
needed after the first runtime suite; seven conversion-audit tests also pass.

The two reference paths contain respectively the three original data blocks
plus a distinct frontier trap, and the pinned 73-block original conversion
graph with an optional native-only data hook. Calibration changes format to 1
in the captured 54E8 GPR/LS input and executes original setup to 5E28. Its SHA256
is `35f13f74e34d1f6884c7c7fb6b1f3d05ef47c9637e4ab001cccf2060e33d6bcf`. This anchor is explicitly synthetic
format 1, not a captured cold root; architectural controls/depths are modeled.

No production routing, runtime switch, ABI, executable, real corpus/live/render
or performance change is claimed. Existing conversion-control-v2 frozen files,
original source/generator/save/template pins and defaults remain unchanged.
Next: complete formats 2 and 3, then integrate all four payloads with separate
switches and padding-aware enclosing admission. The parent must protect its
manager/configuration/callback metadata from every padded write before outer
frame/cursor mutation; pure-data admission alone cannot authorize that parent.
Cold real/live, interpreter, reversed A/B, normalized worker/process CPU and
default gates remain OPEN. Phase 3 and the continuing goal stay active; strict
Stage F reconstruction stays LAST.

## Built-in 24-byte outputs: standalone formats 2 and 3

Both remaining output built-ins now have complete standalone native kernels:

| Format | Original blocks | Per-vertex layout | Native header |
| --- | --- | --- | --- |
| 2 | `5760/5970/5978` | raw XYZ semantic1, CMP11/11/10 semantic2, SNORM16x4 semantic3 | `native_geometry_builtin24_output.h` |
| 3 | `5A30/5D18/5D20` | raw XYZ semantic1, CMP11/11/10 semantics2/3/4 | `native_geometry_builtin24_cmp_output.h` |

Each kernel replaces setup, backedge and loop, ending at the exact queued
PC/function `5648`. Host-local vectors hold the complete pipeline and all
128 final guest register vectors remain exact. Neither path executes a lifted
setup, data loop or tail. CMP retains scale-first FMA operand order, RTZ
signed31 conversion, the actual CF20 shift vector, both shuffles and bit
selection. Format2 also retains the full signed32 SNORM conversion before
packing high 16-bit halves; its intermediate integer results are tested too.
Format3 reuses the read-only stream lookup and 24-byte span admission from
format2, with its own native packing and data pipeline.

Each iteration consumes two vertices and writes three ordered quads at
`+32,+16,+0`, advancing 48 bytes. Every physical quad is aligned/masked as in
the original. Initial reads prefetch three pairs of every stream. Two pairs
are already converted; the next cached pair is converted before stores, then
fresh future pairs are read after all stores, even on the final iteration.
Only nonfinal iterations copy the backedge raw-XYZ cache. These details matter
for overlapping/wrapped streams, output aliases and final speculative state.

Admission rejects foreign LS, all enabled/saved/active interrupt contexts,
zero/odd/greater-than-65,536 rounded counts and every actual destination quad
below E680. It does not mutate context; rejection also preserves the plan.
The low r29 lane controls iteration while upper lanes keep their original
vector arithmetic. At E680, 8,464 rounded vertices end exactly at 40000;
8,466 wrap a padded store into protected LS and reject. Biased pointers and
source/manager wraps are tested. This is pure-data admission: a future parent
must additionally protect its manager/configuration/callback metadata from
every actual padded store before changing its frame/cursors.

Validation for **each format** passes:

- **5,663 strict full GPR/LS/architectural/channel/control/PC/function cases**:
  4,096 broad input/output and cross-stream alias cases, 1,024 arbitrary
  control cases, 256 special-FP cases, wrapped-manager reads, boundary counts,
  biased pointers and three modeled host depths.
- **256 independent ordered byte models**, with raw XYZ/output aliases,
  distinct quantized streams and fresh post-store prefetch.
- **32,772 independent dyadic CMP encodings**, across all 8,193 grid values
  with distinct component triples and signed saturation.
- **828 full original-controller comparisons / 828 native strict child
  roots**, including 48 late outer-frame aliases. Only the selected format's
  data is native; controller/getter remain original on both arms.
- **12 unchanged rejects**, plus **7 conversion-audit tests** for the group.

Format2 additionally passes **131,088 independent full-SNORM32 components**.
Its initial and expanded suites passed without kernel/checker changes. The
initial format3 suite correctly rejected missing setup survivors r76 and then
r79, with output LS already equal. Both survivors were implemented; the whole
suite then passed. Final expanded runs use distinct streams in independent
byte models and include output aliases to format3's fourth stream. Neither
the comparator nor arithmetic contract was weakened. Failure logs are retained.

Each oracle generator pins the original source and captured54E8 anchor, emits
the three original data blocks plus a distinct5648 trap, and separately emits
the full73-block original conversion graph with a native-only child hook.
These original-derived files and captured inputs stay private. Each format uses a
distinct full-graph filename, so generating one oracle cannot replace the other. Calibration
mutates the captured controller format byte and runs original setup to the
new data entry; it is **synthetic, not a captured cold root**. Architectural
controls and host depths are modeled. Reproducible GPR/LS anchor SHA256:

- Format2: `41d9c5666e38f734b77dcf5ea511e694ed704f489c744531e189a5d8cd25a76e`.
- Format3: `c163f89c17cfbe766359683d781c24c25f1e6ca6e96227e9e0774f14f58a4029`.

Run `tests/scripts/test-geometry-builtin24-output.ps1` and
`tests/scripts/test-geometry-builtin24-cmp-output.ps1` for the respective suites.
No production routing, runtime switch, ABI, executable, real corpus/live/render
or performance change is claimed. The conversion-control-v2 frozen files,
source/generator/save/template pins and JOB0/geometry827/newflags0 defaults
remain unchanged. Formats0..3 now all have complete standalone kernels.

Next: add the four production wrappers and independent switches, then native
54E8 built-in dispatch with callback-safe admission before outer mutation.
Real cold roots/live coverage, whole-job acceptance, normal actual-native
frames, interpreter checks, reversed same-frozen A/B and normalized worker/
process CPU gates remain OPEN. There is no new FPS or CPU benefit claim.
Phase3 and the continuing objective remain active, followed by shared phase4,
documented optional5/6 decisions and strict StageF phase7 LAST.

## Built-in output production wrappers (ABI46)

The four complete standalone kernels now have production root wrappers in
the generated image41 adapter. Each has its own strict 0/1/2 environment switch,
default **0**, and distinct stage/fallback ledger:

| Format | Switch | Stage | Root / final frontier |
| --- | --- | --- | --- |
| 0 | `BUZZ_GEOMETRY_JOB_BUILTIN_FLOAT3_OUTPUT` | 51 | `56C8` / `5648` |
| 1 | `BUZZ_GEOMETRY_JOB_BUILTIN20_OUTPUT` | 52 | `5E28` / `5648` |
| 2 | `BUZZ_GEOMETRY_JOB_BUILTIN24_OUTPUT` | 53 | `5760` / `5648` |
| 3 | `BUZZ_GEOMETRY_JOB_BUILTIN24_CMP_OUTPUT` | 54 | `5A30` / `5648` |

Mode0 retains the lift; mode1 runs the entire native payload; mode2 compares
all128 GPRs, all LS, architectural/channel/control state and PC/function at
the exact queued5648 frontier. Whole-job checked mode also makes an enabled
mode1 payload strict. Image41 and ownership by the current native pipeline
are required. Off/foreign/image/post-scope calls go straight to the original
fallback. Missing reference/continuation or rejected data admission records a
fallback without prior mutation. A fallback is never native coverage.

The generators embed private original two-/three-block references for these
checks. They retain the original public5648 continuation but never use native
payloads on the reference arm. Native mode1 never invokes that reference.
The separate closed73-block whole-conversion reference remains original
through every nested child. No checker exclusions, tolerances or state repair
were added. ABI46 and its CMake regeneration guard cover the new hooks;
the generic ledger grows from51 to55 stages.

Production-routing tests pass **576 native /384 strict roots per format**
(2,304 /1,536 total): native mode, whole-job checked mode and independent
payload checked mode, three host depths, sixteen destination biases and four
counts. Other builtin switches are disabled during each format's test, proving
independence. All128 GPR/LS/architecture/channel/control/PC/function and
image/depth/policy/overlay agree with the original. Each format has12 owned
admission rejects preserving the entire context and incoming function queue,
plus off/image/foreign/post-scope checks. Existing driver tests, including
1,024 mock-pipeline scenarios, remain green. Runtime tests pass **424** actual
production-getter cases (including32 new option cases),53 controller/ownership/
mismatch scenarios, durable fallback logging and seven conversion-audit tests.

A new application was compiled and frozen separately. Its hashes are:

- EXE: `7d0074ab97b60c96ff7678609894b8362f5dbe8d62602951bee35a60bfb02852`.
- MAP: `bd8562d96c6721001bdb034f3949cec3ef6379ad127b302e180f45bcf264ba88`.
- generatedC: `f6b24fa8d7ee110c2d318f6e4c768518423d8ab5292f404ffb9e8e99252b983c`.

Five same-frozen-EXE corpus arms pass: checked, off, independent, actual-native
and retained-builtins. **Each replays19,508 real jobs /58,195 transfers exactly**,
with zero concurrent input changes or failures. Checked/independent/retained
arms also have19,508 successful whole-job strict checks and zero whole-job
fallbacks. Every older stage ledger matches its conversion-control-v2 parent.
**All four new builtins have zero real native/strict/fallback roots**: this is
compatibility evidence, not acceptance of a real cold path. Synthetic routing
is recorded separately and is never promoted to captured/live coverage.

These wrappers are usable when the lifted conversion controller reaches a
builtin inside a native-owned job. The native54E8 enclosing controller still
rejects nonempty builtins before mutation. Next: extend its dispatch/admission
to all four payloads and protect manager/configuration/callback metadata from
all actual padded stores before outer frame/cursor changes. That native-parent
integration, expanded cold real inputs,256 actual live comparisons per new
path, actual-native frames, interpreter checks, reversed frozen A/B and
normalized worker/process CPU gates remain OPEN. No new live/render/FPS/CPU
benefit is claimed. Original source, phase2 generator, old frozen apps,
save/template pins and JOB0/geometry827/newflags0 defaults stay unchanged.
Phase3/continuing objective remain active; shared phase4 follows, optional5/6
decisions are documented, and strict StageF phase7 remains LAST.

## Native54E8 built-in output control (ABI47)

The native54E8 controller now executes all four output built-ins as well as
the existing-output, empty and descriptor paths. The complete payloads retain
their independent switches. A new function library supplies the four data
roots and their exact5648 frontier. The controller's own switch remains
`BUZZ_GEOMETRY_JOB_CONVERSION_CONTROL=0/1/2`, default0; all four payload
switches also remain0. ABI47 updates the generated caller/CMake guard;
the generic stage ledger remains55 entries, and conversion-control reporting
now distinguishes all seven paths.

Admission precedes every outer frame/cursor mutation. For nonempty built-ins,
it reads only the selected dispatch/stride table words, low count, output
cursor and actual scalar destination footprint. The selected table targets
must match the original built-in and standard stride; unselected words stay
free to differ and remain visible in the original speculative vector loads.
No original-function clone, data arithmetic, source-stream evaluation or
state repair participates in eligibility.

| Format | Groups | Byte advance | Actual quad offsets per group |
| --- | --- | --- | --- |
| 0 | ceil(count/4) | 48 | 32,0,16 |
| 1 | ceil(count/2) | 40 | 32,16,0,48 |
| 2/3 | ceil(count/2) | 48 | 32,16,0 |

Every actual destination is quad aligned and LS masked. Writes below E680 or
overlapping the live manager's 0x90-byte control region (`+00..+8F`)
reject before mutation. This protects configuration/count/format, cursors,
pointer table and callback fields, including padding-only collisions beyond
the logical output length. Late outer-frame aliases remain supported and
are restored from actual LS. The setup-only semantic-ID quad at manager+160
may be changed by outer-frame saves before data setup, or by data afterward;
the native payload observes the actual setup value, without forecasting it.
Wrapped source/semantic-ID reads and a manager near the end of LS are valid.

Builtin format/stride dispatch ignores custom descriptor/callback fields,
as does the original. Tests explicitly supply null descriptors and nonzero
callback pointers without invoking them. Descriptor paths retain their
callback restrictions. The mock-callee base reader retains its default
builtin exclusion; production explicitly opts into the complete data paths.

Execution preserves the original queued-jump semantics: data entry does not
change r0 or host depth. Its exact queued5648 PC/function is checked before
the controller consumes the frontier and performs the common cursor/stride/
publication/frame-return flow. Descriptor and data switches are independent:
builtin control needs only its used payload and frontier, not the formatter,
seven descriptor encoders or unused built-in function pointers. Disabled or
missing used dependencies fall back with original context/queue intact.

Validation passes **15,988 complete native builtin-controller roots /15,988
native data children**, with both the original73-block graph and separate
closed C reference cross-checking every exit. These cover counts1..65,
all destination/manager/stack biases, three host depths, overwritten late
frame/return values, return-PC collisions, ignored callbacks, selected and
unselected table changes, semantic-ID aliases, minimal stack, wrapped manager
and up to8,192 vertices replacing the whole late frame. **4,472 rejects**
preserve context and plan, including **4,424 padded metadata collisions**;
**1,356 safe near-metadata roots** execute exactly. Initial and expanded suites
passed without kernel, arithmetic or checker corrections.

Production tests additionally pass **3,072 native /1,536 parent-strict /1,536
child-strict roots**: all four formats, four switch combinations, three depths,
sixteen biases and four counts. There are56 unchanged owned dependency/layout
rejects plus12 off/image/foreign checks; rejects execute no native child.
Existing standalone payload routing remains green. Regressions pass1,853
descriptor-control roots with1,341 formatter calls/all7 native encoders,
4,096 stride cases and21 rejects;4,195 base-control cases/96 independent frame
models/14 rejects;424 production-getter cases,53 controller scenarios,
durable fallback logging,1,024 mock-pipeline cases and7 audit tests.

A separately frozen application has hashes:

- EXE: `ccff707a23826a9d1a746203a79fa0d5cdc18edb41650ad4f499f03f03fab36b`.
- MAP: `ed23814bd69d272abcab3d4fc960ddcc7327c540a78a8e3f35360865783877d2`.
- generatedC: `38d862e9c07ebc25b7ae371d710fc384b684107803bcb3cf6c01d3cc7f9b70c7`.

Five same-frozen-EXE arms (checked, off, independent, native and retained
conversion control) each replay19,508 real jobs /58,195 transfers exactly,
with zero concurrent input changes or failures. Enabled native controller
arms each have7,201 descriptor roots/zero fallback; checked and independent
arms have7,201 strict controller roots. Whole-job checked arms each have
19,508 successful strict jobs and zero fallback. Every older generic ledger
matches its previous-wrapper counterpart, accounting for the intentionally
disabled controller in the retained arm. **All four builtin real roots remain
zero**. Synthetic integration/routing is not captured cold or live coverage.

Next: remaining input built-ins1..5 and their shared93F0 entry, expanded real
cold coverage and final acceptance. New builtin real/256-live/native-frame,
interpreter, reversed same-frozen menu/round A/B, normalized worker/process CPU
and default gates remain OPEN. No new live/render/FPS/CPU benefit is claimed.
Original source, phase2 generator, all old frozen apps, saves/template pins
and JOB0/geometry827/newflags0 defaults stay unchanged. Phase3 and the
continuing objective remain active, followed by shared phase4, documented
optional5/6 decisions, and strict StageF phase7 LAST.

## Built-in input format1: complete standalone decoder

Input format1 (`A8F8/AAF8/AB00`) now has a complete standalone native decoder
in `native_geometry_builtin20_input.h`. Each 20-byte source vertex contains
raw XYZ and two CMP11/11/10 words; each pair produces two float4 rows in each
of three destination streams. Position W comes from the actual CD60 constant;
CMP retains shift/mask, signed31 conversion and converted-input-first FMA
operand order. The complete setup, loop and final lookahead are native,
ending at the exact queued9498 PC/function before shared frame restoration.
All128 final guest register vectors, including setup-only survivors, remain
exact. No lifted setup, loop, tail or state repair runs on the native arm.

The shared `native_geometry_builtin_input.h` implements native metadata
reservation/publication and a CMP-vector decoder. Admission's read-only
eight-quad overlay models metadata writes in their actual order: read IDs,
find the matching/free slot with the actual selector, write the semantic ID,
reload the pointer table and publication quad, then publish the selected
pointer using the actual word mask. Three reservations publish semantics1/2/3.
The first IDs read uses incoming r3; subsequent reads use incoming r82.
Slot-table/ID/publication aliases and the no-free-slot original index16
fallback are supported. Eligibility evaluates metadata only, without payload
arithmetic or an original-function clone. Successful admission leaves context
unchanged; rejection also preserves the plan.

The first four source quads are prefetched before metadata writes. A second
40-byte pair is aligned/prefetched after all six metadata stores. Each loop
decodes the cached next pair, writes six ordered destination quads (position
rows, first CMP rows, second CMP rows), then freshly reads four future source
quads, including on the final iteration. Source advances40 bytes, each output
advances32. Only nonfinal iterations copy the source backedge vector. The
future cursor is derived from actual incoming r84, which need not equal r4.

Admission requires owned LS, all enabled/saved/active IRQ contexts absent and
a terminating low r42 counter: negative bytes must be a nonzero multiple32
and at most1,048,576. It checks every actual aligned/LS-masked metadata and
data destination against E680. Destination starts use the actual pointer plus
`r36+r42`, without assuming those vector values cancel. Upper counter lanes
retain their original arithmetic. Data/source/cross-stream and metadata
aliases are preserved, including fresh reads after earlier writes. No parent
frame/control-safety claim follows from this pure-data admission.

Validation passes **5,646 strict full GPR/LS/architectural/channel/control/
PC/function cases**:4,096 broad metadata/source/output/cross-stream aliases,
1,024 arbitrary controls (selector/broadcast, five LS masks, CMP scale/bias,
independent r84 cursor and noncancelling output offsets),256 special-FP scale/
bias cases, boundary counts up to2,048 pairs and three modeled host depths.
**256 independent ordered byte models** include source/metadata publication
overlap, output/source/cross-stream aliases, initial prefetch before metadata
and future prefetch after stores. **8,192 independent CMP words /32,768 float
components** cover every signed11/10-bit code. The expected FMA result is
computed with integer dyadic numerators and explicit24-bit RTZ packing using
the exact stored scale/bias bits; it does not call SPU numeric helpers.
**14 rejects** preserve context, queue and plan.

**3,216 full original93F0 comparisons** pass with **3,168 native strict child
roots**, including48 empty roots, sixteen source/stack/destination biases,
three depths, ignored builtin callback arguments and48 late-frame/return-link
aliases. Shared93F0 setup and9498 restoration remain original on both arms;
only format1 data is native. This is synthetic function integration, not a
newly native enclosing93F0 controller, compiled-app replay or live acceptance.
The first compiled4,099-case suite and expanded suites passed without kernel
arithmetic or checker corrections. The initial header build required replacing
a brace-list range with an explicit array; its failed build log is retained.
Two input-inventory tests and seven conversion-audit tests also pass.

The generator pins the original source and captured54E8 anchor, emits three
private original data blocks plus a distinct9498 trap, and separately emits
an eight-block original93F0 graph with optional native-only data hook and95DC
custom-dispatch trap. The synthetic calibration explicitly supplies a format1
manager/three buffers, then runs original93F0 setup toA8F8. Its GPR/LS SHA256
is `2e75ca50a627366b1ddb692b4f11b9b8742ad07b06f7e450b15140a737f0d81b`. It is not a captured cold input root; architecture/depth are
modeled. Raw captures and all original-derived code remain private.

Run `tests/scripts/test-geometry-builtin20-input.ps1`. The pinned input audit
records23 standard blocks,17 data blocks, the six95C4 routes and the448-byte
shared frame; see [input audit](native-geometry-input-audit.md).
No production routing/switch, ABI, executable, corpus/live/render or FPS/CPU
change is claimed. All builtin-control-v1 frozen files and source/generator/
save/template pins remain unchanged; JOB0/geometry827/newflags0 stay unchanged.
Next: remaining input formats2..5, then production wrappers/native93F0 entry
with parent-safe admission. Before production integration, prove the saved
control pointers used by native callers survive every accepted data write;
pure-data frame aliases alone cannot authorize that parent. Cold real/256-live,
native frames, interpreter, reversed A/B, normalized CPU and default gates
remain OPEN. Phase3/continuing goal stay active; strict StageF phase7 stays LAST.

## Built-in 24-byte inputs: complete standalone formats 2 and 3

Both 24-byte input families now have complete standalone native decoders:

| Format | Original blocks | Input vertex / output streams | Header |
| --- | --- | --- | --- |
| 2 | `A6A8/A750/A860/A864/A868` | raw XYZ +CMP11/11/10 +signed16x4 /3float4 streams | `native_geometry_builtin24_input.h` |
| 3 | `A390/A5D0/A5D8` | raw XYZ +3CMP11/11/10 /4float4 streams | `native_geometry_builtin24_cmp_input.h` |

Each kernel replaces every setup/data/backedge block and queues the exact
9498 PC/function before common frame restoration. Host-local vectors retain
the pipeline, all128 final GPR vectors and all LS/architectural/channel/
control state. No lifted setup, loop, tail, tolerance or state repair is used.
Both reuse the native metadata overlay and CMP-vector decoder from input1.
Input2 adds exact signed16 conversion (`csflt139`) and converted-input-first
FMA with the actual scale/bias; all four components, including W, are decoded.

Each iteration reads a physical48-byte source pair from three aligned quads
and produces two float4 rows per stream. Source pointers retain their raw
bias in the final ABI; source data reads use the original quad alignment,
without input1's byte-alignment shuffle. Initial source quads are loaded
before metadata publication. The next pair is prefetched after all metadata
stores; every subsequent fresh pair follows all six (format2) or eight
(format3) ordered data stores, including on the final iteration. Input advances
48 bytes and each output32 bytes. Nonfinal source-cache copies are explicit;
format2's intermediate nop block is absorbed without changing final state.

Admission reads only metadata/control footprints. It requires owned LS,
all enabled/saved/active IRQ contexts absent, and a terminating negative
low r42 count of1..1,048,576 bytes divisible by32. Every actual quad-aligned,
LS-masked metadata/data store must stay at or above E680. Data destinations
use the actual slot pointer plus r36+r42; cancellation is not assumed. Metadata
source/destination addresses follow each original family:

- Input2: first IDs read/write through r3, second read through r3 **but write
  through r82**, third read/write through r82. Three reservations publish
  semantics1/2/3 via incoming r35/r37/r61.
- Input3: first two IDs read/write through r3, last two through r82. Four
  reservations publish semantics1/2/3/4 via r35/r37/r61/r39.

The shared reservation helper now accepts a distinct IDs store address;
the existing same-address overload retains input1 behavior. Input2's first
suite reached the split-manager case at trial5635 and detected incorrect
GPR/LS output from writing the second IDs quad to its read address. The helper
and input2 caller now preserve the original separate read/store destinations.
All input2 cases then pass, and the complete input1 regression also passes.
The numeric/checker contracts remain unchanged. The failed comparison log is
retained. Input3's complete first suite passes without runtime corrections.

Validation passes **5,678 strict full-state/LS/PC/function cases for input2**
and **5,710 for input3**. Each suite includes4,096 broad metadata/source/output/
cross-stream aliases,1,024 arbitrary-control cases,256 special-FP coefficient
cases,32 split-manager cases, boundary counts up to2,048 pairs and three
depth anchors. Input3 additionally has32 fourth-publication/IDs aliases.
Each has **256 independent ordered byte models**, including source/metadata
overlap and post-store prefetch, plus **8,192 independent CMP words /32,768
float components**. Input2 additionally covers **all65,536 signed16 codes /
262,144 independent components**, checking both the full converted float and
scaled result. Its expected fused result uses the exact integer numerator
`0x800080*(2*signed16+1)*2^-39` with explicit24-bit RTZ packing, without SPU
numeric helpers or a floating-point tolerance.

For each format, **3,216 full original93F0 cases /3,168 native strict data
children** pass:48 empty roots, all sixteen source/stack/destination biases,
three host depths, ignored builtin arguments and48 late-frame/return-link
aliases. Only the selected data family is native; original setup and9498
restoration remain on both arms. Input2 has14 unchanged rejects; input3 has17,
including fourth-publication and fourth-stream protected writes. Rejection
preserves context, plan and incoming function queue. Two input-inventory and
seven conversion-audit tests also pass.

The private references are the five/three original data blocks with distinct
9498 traps and separate ten-/eight-block original93F0 graphs with native-only
data hooks and95DC traps. Calibrations mutate the synthetic manager format
argument and execute original setup; they are not captured cold roots.
Reproducible GPR/LS SHA256:

- Input2: `9a584efd407eebf6c93d85585d9b0943e88013ab8e2156ad2fc7c36fe71638b6`.
- Input3: `1dd49c03f02f3c348d863359498ab47c237cf833c586c106a50894e49bac485a`.

Run `tests/scripts/test-geometry-builtin24-input.ps1` and
`tests/scripts/test-geometry-builtin24-cmp-input.ps1`. No production routing,
runtime switch, ABI, executable, real corpus/live/render or FPS/CPU change is
claimed. Input1's full5,646-case/256-model/32,768-component/3,216-original-root
regression verifies the shared helper change. All builtin-control-v1 frozen
files, original source/generator/save/template pins and JOB0/geometry827/
newflags0 defaults remain unchanged.

Next: required input4/5 packed families, then production wrappers and native
93F0 shared control with admission that protects native callers' saved control
pointers before unsafe mutation. Pure-data frame aliases do not prove that
parent safe. Cold real/256-live/native-frame, interpreter, reversed frozen
A/B, normalized CPU and default gates remain OPEN. Phase3/continuing goal
stay active; shared phase4 follows, optional5/6 decisions are recorded and
strict StageF phase7 remains LAST.

## Built-in 12-byte input: complete standalone format4

`native_geometry_builtin12_input.h` now replaces complete `9E70/A170`:
fixed12 XYZ plus two packed-unit vectors, ordered metadata publication,
delayed normal/tangent stores, table/source alias timing, final flush and
strict full9498 ABI. The [input audit](native-geometry-input-audit.md#built-in-input-format4-complete-standalone-decoder)
records the byte/numeric map, exact eligibility and evidence limits.

Validation passes5,806 strict full-state/LS/PC/function cases,288 ordered byte
models,3,145,728 independent integer position components and3,216 full
original93F0 comparisons with3,168 strict native data children. Sixteen
rejections preserve context/plan/queue, including both extra delayed-output
footprints. Two input-inventory tests pass. Byte models use SPU arithmetic
for unit reconstruction; their unit numeric operations are not independently
validated. Full-state comparisons retain the exact original estimate path.
Only input4 data is native in the integration harness; shared93F0 setup and
9498 restoration are original on both arms. Synthetic coverage remains
separate from cold real/live acceptance.

No production routing/switch/ABI/app/default changes and no FPS/CPU benefit
claim. Frozen pins and JOB0/geometry827/newflags0 stay unchanged. Required
input5, production wrappers/native93F0 and cold/final gates remain open.
The continuing objective stays active through shared phase4 and final strict
StageF7, with optional5/6 decisions recorded before StageF cleanup.

## Built-in 15-byte input: complete standalone format5

`native_geometry_builtin15_input.h` now replaces full `97F8/9BB0`: fixed12 XYZ
and three packed-unit vectors, all four metadata publications, delayed-output/
table/source ordering, final flush and exact full9498 ABI. The [input audit](native-geometry-input-audit.md#built-in-input-format5-complete-standalone-decoder)
records parameter-base differences, actual read/store timing and provenance.
Shared gathers, position arithmetic and table composition are factored into
`native_geometry_builtin_fixed_input.h`; format4's full regression passes.

Format5 passes5,838 strict complete-state cases,288 ordered byte models,
3,145,728 independent integer position components,3,216 original93F0 cases
with3,168 strict native data children and20 unchanged rejects. Unit numeric
operations in byte models use SPU primitives; no independent unit numeric
oracle is claimed. The first compiled suite passes without runtime/checker
corrections. Shared93F0 setup and9498 restoration remain original on both arms,
and the calibration is synthetic rather than a real cold builtin capture.

All17 builtin input data blocks now have implementations: existing raw0 and
standalone formats1..5. Production wrappers/native93F0 remain open, followed
by cold real/live/render/interpreter/reversed A/B/normalized CPU/default gates.
The frozen app, ABI47,55-stage ledger, original pins and JOB0/geometry827/
newflags0 defaults remain unchanged. No FPS/CPU gain is claimed. The goal stays
active through phase3, shared phase4, optional-phase decisions and strict
StageF7 last.

## Complete standard93F0: standalone native control and data

`native_geometry_input_control.h` now implements the complete shared frame,
empty path,95C4 dispatch, composition of native input formats1..5 and late9498
restoration. Raw0 reuses its established native full-root implementation and
admission limits. All128 GPRs, LS, architecture, channels, control, PC/function,
image/depth/policy/overlay remain strict, with no original native-arm blocks,
state repair or expanded StageF exception.

The [input audit](native-geometry-input-audit.md#complete-standard93f0-control-standalone-composition)
records the40-quad frame/metadata forecast, actual late-frame aliases and limits.
Validation passes8,391 complete native/original93F0 roots,7,796 full-state
pre-data boundaries and8,391 independent frame byte models. All23 rejects
preserve context/plan/queue. Roots cover all six standard formats, empty calls,
biased managers/stacks, source/parameter/metadata/return-link frame aliases,
wrapped stacks, arbitrary mask quads and only-selected-target validation.
Full regressions of all five standalone decoders pass unchanged after factoring
shared metadata admission. The initial fixture's incorrectly encoded biased
pointer table was corrected; kernel arithmetic/checkers were unchanged.

This is synthetic function composition, not production routing, real cold
coverage, a new frozen executable or live acceptance. No switch/ABI/default
changes and no FPS/CPU gain are claimed. Next: protect native parent control
assumptions and scratch/table/secondary-stream footprints before production
integration, add independent switches and compiled-app checks, then complete
all cold/final gates. Existing frozen app/ABI47/55 stages and JOB0/geometry827/
newflags0 remain unchanged. Phase3/continuing goal stay active through shared
phase4 and final strictStageF7.

## Production standard93F0/input wrappers: ABI48/61 stages

Complete input control and formats1..5 now run through independent default-off
production switches, with parent whole-return and child full9498 state checks.
The [input audit](native-geometry-input-audit.md#production-standard93f0-and-builtin-input-routing-abi48)
records the exact parent/scratch/descriptor contract, switch dependencies,
validation limits and frozen bindings.

Production routing passes3,876 roots/1,938 parent-strict/1,440 child-strict
comparisons. Parent validation adds576 original/native roots across all36 format
pairs and292 layout cases/877 capacity/control rejects, including pure-custom
and mixed semantic0 descriptors. The8,391-root controller suite, all five full
data suites,1,024 old modeled driver scenarios,472 production-getter cases and
53 controller scenarios pass. Input/conversion audit tests pass2/7.

All five final frozen corpus arms pass19,508 exact jobs/58,195 transfers, zero
input divergence/failure. Enabled whole jobs execute19,508 natively with zero
fallback. The controller wrapper has19,508 real raw0 roots and zero fallback;
checked/independent arms add19,508 strict input-root comparisons. Raw0 retains
its old complete native helper; formats1..5/new common composition remain
synthetic with zero real/live roots. Older ledgers match after raw-stage migration.

The initial integration's overlarge declared descriptor-span forecast reduced
native whole-job coverage by5,838 despite exact final outputs. That coverage
regression was rejected and corrected from an actual C++ entry audit. Metadata
needs the8-byte header/8-byte rows; speculative trailing quad reads still use
actual LS. No arithmetic/checker relaxation was made. Final parent guards also
apply when the new controller is off, before any preamble write. Failed V1 and
passing intermediate V2 artifacts are retained separately.

Final freeze: `build-vulkan-phase3-input-routing-v3`, ABI48/61 stages,
EXE`bec8b85965eb662a171004af33060e30dc0e868a50e016d2c4cb45e2ea6efbd6`,
MAP`729a12aef82a51d8c8ddeb5810345681440886c734583ae5043725e55023da5d`.
Thirteen frozen native sources and the generated C bind the implementation;
older source/save/template/executable pins stay intact. No FPS/CPU gain is
claimed; replay timings are functional measurements. JOB0/geometry827/newflags0
stay unchanged. Next: live checks/actual-native menu and round rendering,
interpreter counts/reversed A/B/normalized CPU and cold-path evidence. Phase3
and the continuing goal remain active; shared phase4 follows and strictStageF7
remains last.


## ABI48 live whole-job/input and actual-round rendering

The unchanged `272a375` implementation and frozen
`build-vulkan-phase3-input-routing-v3` pass the live whole-job/raw0 and native
scene-frame gates with native Stage F (`BUZZ_STAGEF=1`). The checked menu
completes 80s and records at least **45,000 strict whole-job/I/O comparisons
and 45,000 strict raw0 input returns**, exceeding the 256-call gate.
The 75s native menu records at least 2,750,000 raw0 calls; the visually
verified round navigation/run records at least 11,415,000. All three runs
have no mismatch, halt, first-fallback or unrecordable messages. These are
periodic lower bounds after forced shutdown, not final fallback totals.

Inspected 1280x720 renderer readbacks show a normal purple/blue studio menu
and an actual green single-player question: the river-flow question, four
answers, player Adam and host are visible with readable UI and normal
geometry. Exact PPM/PNG pixel bindings are recorded privately. Visual
checkpoints confirm the main menu, challenge list, host intro, join screen,
challenge intro and actual question; the `solo-round` timing marker alone
is not treated as scene proof. The paused run continues 60s after visual
acceptance and is not a performance measurement. Presented frame counts
are 1,097 checked-menu / 2,166 native-menu / 9,204 round including navigation.

Private `input-routing-v3-live-summary.json` binds the three logs/CSVs,
phase records, frozen EXE/MAP and inspected images. All five new builtin
input formats still have **zero real/live calls**; raw0 uses its existing
complete native helper. This result does not close their cold-path gates.
No implementation, checker, ABI or default changed: JOB0/geometry827/newflags0.
Interpreter-count checks, reversed same-frozen menu/round A/B, normalized
worker/process cycles and CPU time, remaining cold real/live paths and default
decisions remain OPEN. Phase3 and the continuing objective stay active;
shared phase4 follows and strict Stage F cleanup remains phase7 LAST.


## Map-bound interpreter diagnostics on ABI48

Four fresh diagnostic runs use the unchanged frozen input-routing V3 EXE/MAP,
StageF1 and leaf mask827, with the same initial private save. The new
`perf/observe-spu-interpreter.py` reads six existing interpreter-statistics
symbols directly from that exact map and records the raw bytes. It does not
write memory, suspend threads or sample CPU cost. `SPU_INTERP_STATS=3600`
prevents a statistics reset during these short fresh runs; entry diagnostics
are enabled as a separate inventory. Eight tests cover disabled/reset/partial
or inconsistent counters and a single instruction below the logger threshold.

| Scene/arm | Renderer-relative snapshots | Interpreter observations |
| --- | --- | --- |
| Menu / JOB0 | 15s,60s | Absolute total and all64 buckets zero; statistics uninitialized(-1) |
| Menu / JOB1 | 15s,60s | Same absolute-zero result |
| Round / JOB0 | 15s,240s | Zero at15s;300,699,624 observed at240s,all in image2/five pages |
| Round / JOB1 | 15s,240s | Zero at15s;300,952,932 observed at240s,the same image2/five pages |

The two round totals differ by about0.084%; this is diagnostic information,
not proof of exact equality or a performance result. Positive legacy counters
use non-atomic increments; a timed window also does not fix the number of
completed jobs. The whole-run entry inventories match: image2 at07B4C/A6C8
and image40 at02A18. The image40 entry is later than the240s snapshots. No
image41 interpreter bucket or distinct entry is observed, but that absence
is not promoted to an exact per-image instruction-count proof.

Both rounds reach actual green single-player play, verified with preserved
1280x720 PPM/PNG readbacks (reference question/answers; native round/result).
The diagnostic helper waits50s through the host intro rather than26s; both
navigation markers occur near155.2s and the runs finish near295.5s. These runs
include occasional readbacks and an observer and are excluded from FPS/CPU A/B.
`interpreter-v1-summary.json` binds the raw samples, logs, phase records, initial
save/EXE metadata and inspected images. The original/compiled game code,
strict checkers, Stage F exception and JOB0/geometry827/newflags0 defaults
remain unchanged.

The menu interpreter gate passes. The round count gate stays **OPEN**:
pre-existing runtime interpretation must be distinguished from additional
geometry interpretation with evidence suited to the completed workload.
Reversed same-frozen menu/round FPS A/B, separate normalized worker/process
CPU/cycles, cold real/live paths and default decisions remain OPEN. Phase3
and the continuing objective remain active through shared phase4, optional5/6
decisions and final strictStageF7.


## First complete ABI48 whole-job menu A/B and CPU series

The [separate performance report](native-geometry-phase3-performance.md) records
same-frozen-EXE JOB0/JOB1/JOB1/JOB0 FPS and separate CPU series, each with the
same initial save, StageF1, geometry mask827 and40..98s menu window. Seven
report tests and all eight completed measurement runs pass their validity
checks, including actual native coverage and raw counter reconstruction.

The combined native configuration regresses FPS **27.74 ->25.30 (-8.78%)**;
both pairs agree. Worker cycles/frame rise **15.92%**, process cycles/frame
**9.19%**, worker CPU ms/frame **16.02%** and process CPU ms/frame **9.13%**,
all in the same direction in both reversed pairs. Do not enable this combined
configuration by default. This aggregate result does not identify which
individual functions cause the cost; the next step is separate diagnostic
profiling and targeted optimization, with strict validation retained.

The first trial activated replay via an empty environment variable and exited;
it is invalid and preserved separately. Corrected measurements remove replay
and dump variables entirely. A later screenshot-helper pre-initialization
change keeps future round captures from compiling C# during a running scene;
the exact earlier menu scripts are archived and the game EXE is unchanged.
Round A/B/CPU, round interpreter counts, cold real/live and final/default gates
remain OPEN. JOB0/geometry827/newflags0 and the StageF exception remain unchanged.
Phase3 and the continuing objective stay active through shared phase4,
optional5/6 decisions and final strictStageF7.


## Profile-guided formatter column proof: opt-in

Separate V3 native/reference menu RIP samples identify formatter admission
as3.57% of active native pool leaf samples. These diagnostics perturb timing
and are excluded from FPS/CPU measurement. The
[performance report](native-geometry-phase3-performance.md#diagnostic-profile-and-formatter-admission-candidate)
records their scope; the
[conversion audit](native-geometry-conversion-audit.md#formatter-column-span-proof)
documents the resulting conservative address-span proof.

The new default-off FORMATTER_SPANS0/1 option accelerates regular read-only
admission and retains exhaustive checks for every unproven column. It preserves
signed/modular addressing, dummy/cross writes, metadata protection, iteration
budgets and admission coverage. Data execution, all-register/LS/control/I/O
checks and the accepted Stage F contract are unchanged. ABI48/61 stages remain.

50,000 independent quad models and the existing1,908 formatter/1,853 conversion/
15,988 builtin strict suites pass, as do routing,1,024 mock drivers and480
getter cases. Six new-frozen corpus arms each pass19,508exact/58,195transfers,
zero failures/input changes and unchanged ledgers; native whole jobs have zero
fallback. Live option1 reaches>=35,000 strict formatter and>=45,000 whole-job/I/O
comparisons. EXE`b95e457fc251aff615fc284a94c60e827969611ba917ff95b595220e1792dc8b`
and MAP`7ae7077e2141dc60f8b26181164b0d0efcc2182db11b7be4a26549daf843cef1`
are frozen separately from V3, with15 native source bindings.

No gain is claimed before measurement. This new freeze still needs native
menu/round frame acceptance and own-switch reversed A/B/normalized CPU checks.
All existing cold real/live, round interpreter and remaining phase3 gates stay
OPEN. JOB0/geometry827/newflags0 remain; phase3/continuing goal stay active
through shared phase4,optional5/6 decisions and final strictStageF7.


## Formatter span proof: native frames and isolated menu benefit

The b95e457f/7ae7077e freeze passes normal actual-native menu and actual-question
round GPU readbacks (JOB1/SPANS1/StageF1),with exact PPM/PNG bindings and actual
native formatter coverage. The round question,answers,timer,host and Adam UI
are visible;the scene is not inferred from draw counts or a navigation marker.

The benchmark runner/report now support an isolated production option while
retaining JOB1 in **every** arm. Ten tests include rejection of changing other
options,wrong JOB mode and a mismatching runtime span marker. Four fresh FPS
and four separate CPU menu runs use SPANS0/1/1/0,mask827/StageF1,the same initial
save,EXE/MAP and40..98s window. Both FPS pairs improve:25.01 ->25.73(+2.88%).
Worker cycles/frame fall3.17%,process2.07%;worker/process CPU ms/frame fall4.25%/
2.40%,with both pairs agreeing. The
[focused performance report](native-geometry-phase3-performance.md#formatter-span-proof-isolated-menu-measurements)
records exact values and limits.

SPANS stays default0 until round reversals/CPU and remaining gates pass. This
individual benefit does not prove the entire native JOB1 configuration faster
than JOB0;the earlier aggregate regression remains independently recorded.
Round interpreter counts,cold real/live paths and phase3 completion remain open.
The continuing objective stays active through shared phase4,optional5/6 decisions
and strict StageF7 LAST. Original saves,sources,checkers and frozen apps remain
unchanged;no new game build was made for this measurement increment.


## Pre-window round capture correction

Rejected the first isolated round timing attempt:its195s PrintWindow PNG was
black,so no round FPS/CPU result or scene gate was accepted. The partial run,
raw files and pinned scripts remain private. Windows Graphics Capture also
failed twice with CreateForMonitor0x80070057 in a separate functional test.

Added an opt-in single timed GPU readback in SDKcommitac0f6aef,without enabling
continuous readback. The runner requests190s,converts the lossless image at195s
and measures200..270s. The reporter binds the actual capture to its CSV frame,
requires completion before195s and readback0,compares native PPM/PNG pixels and
still requires manual inspection. It rejects duplicate/late/continuous captures
and changed native pixels. Thirteen real-GPU smoke tests and13 reporter tests
pass;normal periodic dumps and explicit readback suppression also pass.

The new3df8d057 frozen EXE has normal inspected native menu and actual green
challenge/result-arena images,each from one GPU readback bound to a timing row.
The round image shows Poprawne0,the host and Adam robot;no question-screen
claim is made. All15 frozen native geometry sources and generated C match the
prior formatter-spans freeze exactly;ABI48/61 stages,strict state comparison,
defaults and the narrow StageF contract exception remain unchanged.

This capture repair is necessary to make the round performance gate reviewable.
It does not establish a speed change. The earlier menu benefit belongs to the
b95e457f EXE;new-EXE round FPS/CPU and repeated menu pairs are still required.
JOB0/geometry827/all new flags0,including SPANS0,remain the defaults.
Phase3 and the continuing phase4/optional5/6/final strictStageF7 objective remain
active;round interpreted counts and cold real/live gates remain open.


New-freeze validation after the capture repair:all six replay arms each pass
19,508exact jobs/58,195transfers,zero divergent inputs/zero failures. Enabled
arms execute19,508native jobs with zero whole-job fallback;every earlier stage
ledger is unchanged,and SPANS0/1 agree. A separate80s checked menu run passes
at least45,000 strict whole-job/I/O and35,000 strict formatter returns,without
DIFF,first-fallback or unrecordable errors. Periodic live counts are lower
bounds,not final fallback totals. These correctness checks do not replace
cold real/live,interpreter or new-EXE performance/default gates.


## Isolated formatter span proof: completed round FPS reversal

The new3df8d057 frozen EXE completes all four SPANS0/1/1/0 round FPS arms,
with actual JOB1/all other production options1 in every arm. The public report
passes complete200..270s windows,EXE/MAP/save/settings/hash/native coverage,
exactly one pre-window GPU capture with readback0,lossless PPM/PNG and four
manually inspected actual-round scenes. There are no DIFF,first-fallback or
unrecordable markers. The earlier black PrintWindowV1 remains rejected.

Mean FPS30.93 ->31.52,+1.91%;both reversed pairs improve(+2.87%,+0.96%).
Frame-time tails are mixed:the third native run has p99=54.80ms versus44.06ms
in its paired control,while the first native pair improves43.58ms versus44.18ms.
Questions and camera phases differ,and one navigation marker is later;all
images still prove actual round content before the fixed measurement window.
The mean result does not close the tail gate or establish a cause for p99.
See the [full performance report](native-geometry-phase3-performance.md).

No new CPU result or aggregate JOB0/JOB1 benefit is claimed. Separate round
CPU,repeat menu FPS/CPU on the same new EXE and round tail follow-up remain
required. JOB0/geometry827/newflags0/SPANS0 remain defaults;strict checkers,
ABI48/61 stages and the StageF exception are unchanged. Phase3 and subsequent
phases stay active,with strict StageF7 last. Private report and scene QA are
`spans-fps-round-v2-report.json` and`spans-fps-round-v2/scene-qa.json`.


## Isolated formatter span proof: completed round CPU reversal

Four separate CPU runs on the same3df8d057 EXE use SPANS0/1/1/0 with actual
JOB1/all other production options1 throughout,common827/StageF1 and initial
save. The public report recomputes raw map-bound200..270s snapshots,checks
stable named lane identities/creation times,boundaries/frame uncertainty and
all hashes,and accepts four inspected normal native round images. Each has
one pre-window GPU readback,continuous readback0 and exact PPM/PNG pixels.
No DIFF,unrecordable or first-fallback marker appears.

Worker cycles/frame fall4.01%(pairs-3.28/-4.74),worker CPU ms/frame4.45%
(-3.38/-5.51);whole-process cycles/frame fall2.82%(-2.48/-3.17),CPU ms/frame
3.38%(-2.96/-3.80). Both pairs agree for all four metrics. This is an isolated
option saving,not an aggregate JOB0/JOB1 result or power measurement. The
separate FPS mean is+1.91%;its mixed p99 remains unresolved. CPU-series p99
is lower in both native arms,but this contextual result does not replace
independent FPS tail follow-up. The [performance report](native-geometry-phase3-performance.md)
contains the exact cost table and per-pair values.

The suspected phase/renderer timing offset was checked against original file
UTC metadata for all8 round runs:PNG conversion completed194.76..194.97s after
CSV creation,before200s. Monotonic native capture/frame timing remains primary;
the filesystem check is supplementary and is preserved with hashes.

New-EXE menu FPS/CPU,round tail follow-up,cold real/live,round interpreted counts
and final defaults remain open. JOB0/geometry827/allnewflags0/SPANS0 and all
strict checkers remain unchanged. Phase3 and the continuing objective stay
active,with mandatory strict StageF7 last. Private evidence includes
`spans-cpu-round-v2-report.json`,`spans-cpu-round-v2/scene-qa.json` and
`spans-round-v2-clock-alignment.json`.


## Isolated formatter span proof: same-EXE menu FPS and CPU

Eight fresh menu runs on the same3df8d057 EXE/MAP9b027e64 complete the
SPANS0/1/1/0 FPS and separate CPU series. Actual JOB1/all other production
options1,audit0,geometry827/StageF1 and initial private save stay fixed.
The public reports accept complete40..98s windows,all hashes/runtime options,
nonzero actual native coverage and raw CPU/lane/boundary/frame checks. No GPU
captures,interpreter observer,concurrent build,test or profiler is used.
No DIFF,first-fallback or unrecordable marker appears.

Menu FPS25.00 ->25.64,+2.56%;pairs+1.07/+4.07. Median,p90 and p99 improve
in both native pairs. Worker cycles/frame fall2.99%(-2.94/-3.05),process1.84%
(-1.91/-1.78);worker CPU ms/frame falls1.16%(-1.46/-0.86),process1.19%
(-1.14/-1.23). Both CPU pairs agree for every metric. CPU-series FPS is
contextual;the separate FPS report remains primary. See the
[performance report](native-geometry-phase3-performance.md) for exact tables.

Menu and round now have their own FPS/CPU reversals on one frozen EXE. This
establishes isolated option measurements,not aggregate JOB0/JOB1 savings or
completion of phase3. One first-series round native p99 remains worse;the
next step is an independent round FPS repetition on the same3df EXE. Keep
SPANS0/JOB0/geometry827/newflags0 until the relevant defaults are justified.
Cold real/live,round interpreted-count,whole-job and final gates stay open;
strict checkers/ABI48/61 stages/StageF waiver are unchanged. The continuing
objective remains active through shared phase4 and final strict StageF7.
