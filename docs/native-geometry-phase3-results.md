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

This core is **not connected to the game yet**. The tests do not establish
live transaction capture, real list/atomic coverage or native driver speed.
There is no new enabled runtime path or changed phase-2 default.

## Remaining phase-3 gates

- Integrate channel recording and the DMA observer without replacing corpus
  capture/replay observers or issuing shadow guest writes. Validate real list
  descriptors, transaction ordering and unsupported/asynchronous traces;
  rejected traces cannot count as comparisons.
- Add the independent off/native/checked job switch and an eligibility check
  on the original entry before mutation. Preserve per-kernel switches and
  whole-job fallback for unsupported descriptor families.
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
