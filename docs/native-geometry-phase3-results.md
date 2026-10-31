# Native geometry phase 3: implementation and evidence

Status: **in progress**, 2026-10-31. Phase 2 ended at `ef906be`, with default
geometry mask 827 (`0x33B`). The continuing objective in
[native-geometry-plan.md](native-geometry-plan.md) authorizes advancing through
subsequent phases after their gates, with strict Stage F reconstruction last.
The outer driver is native; the complete native data path remains open.

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

## Remaining pipeline inventory

This table tracks the outer driver's library boundaries explicitly. Completing
a row still requires its supported formats/paths and normal gates; native
coverage of one leaf does not make the entire enclosing stage native.

| Library entry | Role | Native status |
|---|---|---|
| `0xAC68` | Primary/secondary attribute decode | Retained library; validated format kernels inside |
| `0x0BF8` | Additional streams/blend and DMA lists | Retained library |
| `0x09F8` | Palette/skinning flow | Retained library; validated skin kernel inside |
| `0xAEA0` | Index decoding | Retained library; validated index/triangle kernels inside |
| `0x6608` | Transform/cull | Retained library with existing Stage F; strict cleanup remains phase 7 |
| `0x2B80` | Output buffer sizing | Retained library |
| `0x37C0` | Ring allocation/atomics | Retained library |
| `0x2DC0` | Index publication | Native opt-in path above; other input families retain library |
| `0x54E8` | Vertex conversion/packing | Retained library; validated output kernels inside |
| `0x35F0` | Vertex publication | Retained library |
| `0x12F0`, `0x02E8` | Full/position-only command emission | Retained library |
| `0x32C8`, `0x27A0` | Empty-output conversion/publication | Retained library |

The host input model, context initialization and complete outer control are
native. Host-buffer/data-path integration and the retained stage bodies above
remain phase-3 work; this publication increment does not close the phase or
enable a new default.

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
