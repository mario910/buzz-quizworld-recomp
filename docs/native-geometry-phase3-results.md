# Native geometry phase 3: implementation and evidence

Status: **in progress**, 2026-10-31. Phase 2 ended at `ef906be`, with default
geometry mask 827 (`0x33B`). The continuing objective in
[native-geometry-plan.md](native-geometry-plan.md) authorizes advancing through
subsequent phases after their gates, with strict Stage F reconstruction last.
This report does not claim that the native whole-job driver exists yet.

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

## Remaining phase-3 gates

- Add the independent off/native/checked job switch and an eligibility check
  for supported descriptor families on the original entry before mutation.
  Preserve per-kernel switches and whole-job fallback. The diagnostic audit
  switch above is not the native-path switch.
- Implement the native descriptor/pipeline flow using the validated routines.
  Keep host state between stages where all observable LS/GPR/IO effects remain
  exact. A renamed or merged lift alone does not complete this requirement.
- Pass the complete 19,508-job / 58,195-transfer image-41 corpus in checked
  and actual-native execution; expand coverage for newly supported paths.
- Record at least 256 strict live job comparisons, normal actual-native menu
  and round frames, and unchanged interpreted-SPU counts.
- Complete same-EXE reversed menu/round FPS and matched CPU-cost measurements,
  decide the default from evidence, validate the selected configuration,
  finalize this report and commit phase results before advancing to phase 4.

The active phase-3 goal remains unfinished. Subsequent native phases continue
under the user's standing authorization once these requirements are met.
