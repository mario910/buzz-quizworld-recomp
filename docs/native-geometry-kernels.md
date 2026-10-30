# Native image-41 kernels

Phase 2 is in progress. The phase 0/1 base is commit `719f325`. New kernels
use strict state checks; the existing Stage F exception remains unchanged.

## Runtime controls

`BUZZ_GEOMETRY=0` forces the lift, `1` runs selected native kernels and `2`
compares every handled invocation against the lifted reference. The reference
state is retained in checked mode. `BUZZ_GEOMETRY_KERNELS` is the function bit
mask; its current default enables bit 0 when that implementation is built.
Bit 0 selects the coupled unit-vector
decoder below. Other function IDs are reserved while their implementations
are pending; the build advertises its implemented mask and rejects unavailable
bits. An unhandled input takes the reference and increments fallback;
it is never counted as a successful comparison. Mismatches abort with the
first register, LS and control-state differences. Offline checked replay also
fails if a selected kernel makes zero comparisons.

Generate/build with `tests/scripts/build-geometry-phase2.ps1`. It retains
the existing Stage F wrappers, selects the new generated geometry source,
requires the audited AVX2/FMA image-41 RTZ build, and refreshes the executable
manifest. Derived reference code stays under ignored `analysis/`.
The normal `scripts/build-windows.ps1 -StageF` build also selects this adapter
for the exact reviewed EU 1.00 lift with native AVX2/FMA enabled. Other source
hashes retain the existing Stage F generator and its own guards.

## Bit 0: unit vectors (`0x9148/0x92A0`)

The entry is `0x9148`; the exit is `0x83C8`. `0x9148` sets up a column and
`0x92A0` completes its pipelined groups and advances to the next column.
The native routine covers the remaining columns of this pure region. The
disabled wrapper preserves the original block boundary and continuation.
There are no DMA, channel operations or nested lifted calls in this region.

`native_geometry_unit24.h` decodes eight packed 24-bit unit vectors per AVX2
batch. It uses the documented two 10-bit fields and axis/sign nibble, exact
SPU FMA/FM/FA/FS/FRSQEST/FI operations, then transposes XYZ/W for BE float4
stores. It leaves the last column to the original pipeline. That column
reconstructs all 128 registers, including dead setup temporaries, speculative
padded loads and the final aliased sentinel-store bytes. Strict checks apply
to that complete output, not only the useful vertex data.

The fast path requires the pinned constant/mask table, the reviewed 16-column
layout, at least 64 vertices, aligned input bases, bounded input/output,
nonoverlapping source/output/sentinel ranges and valid selector axes. Debug
probes, pending interrupts, external LS, smaller or different layouts take
the lift. All rejection checks precede native writes. The wider scope avoids
counting `0x9148` as a second independent optimization.

`tests/scripts/test-geometry-phase2.ps1` runs 400 synthetic full-state cases:
varying stream strides, attribute offsets, starting columns, routing low bits,
sizes and padding. It also checks that external LS, aliasing, unaligned/wrapped
input, changed masks/tables, pending interrupts and the last-column-only case
are rejected without changing state or continuation. An optional private
entry snapshot supplies a diagnostic, reversed-order microbenchmark:

```powershell
.\tests\scripts\test-geometry-phase2.ps1 -Snapshot analysis/native-geometry/snapshots/p1-menu/pc_9148.bin
```

On that one 408-vertex menu sample, the decoder measures roughly 4.2 us lifted
versus 2.4 us native. This is a local function microbenchmark, not an FPS claim.

Corpus validation: all 19,508 image-41 jobs / 58,195 transfers match, with
zero concurrent-input changes. The checker compared 6,628 supported decoder
groups exactly; 5,439 unsupported groups used the reference. A separate native
execution replay also matches all jobs. Keep Stage F in mode 2 for these
whole-job checks: mode 1 has its accepted dead-register differences, especially
on culled jobs. The new kernel's strict comparisons never waive those fields.

Private logs are `analysis/native-geometry/phase2/unit24-final-mode{2,1}.log`.
`unit24-build.json` binds the frozen validation executable to the generated
source, handwritten kernel and build-support hashes. Live checked mode logged
at least 600,000 strict matches, with no differences, and the menu renderer
dump looks normal. Native/reference round-scene introduction dumps also look
normal. Logs: `analysis/vk-scene-p2-unit24-live.stderr.log`; images remain private.

The four menu runs (40..98 s, reference/native/native/reference) yield
31.56 / 31.99 / 31.89 / 31.64 FPS: means 31.60 -> 31.94 (+1.08%). The four
round runs (150..220 s) yield 38.44 / 40.40 / 40.90 / 40.18 FPS: means
39.31 -> 40.65 (+3.41%). Pair gains are +1.38%, +0.78% in menu and +5.09%,
+1.80% in round. Both scene reversals pass. Default bit 0 is enabled after
these checks; `BUZZ_GEOMETRY_KERNELS=0` or `BUZZ_GEOMETRY=0` restores the lift.
The measurement set uses one frozen executable, fresh identical save copies,
Stage F mode 1, unchanged renderer/audio settings and background mailbox input.
Round runs take one occasional GPU readback at every 6,500 presents in both
modes; the displayed frames are introductions in the round scene, not question
screens. These measurements are compared within their set, not against the
older 36.3/44.2 FPS baseline taken in a different session.

`perf/measure-geometry-kernel.ps1` runs A/B and B/A in both scenes; later steps
can supply `-BaselineMask` to keep previous kernels enabled. Summarize a complete
eight-run set with `perf/geometry-ab-report.py`; it rejects incomplete runs,
different executable hashes and incorrect reversal order.
The source-linked report is `analysis/native-geometry/phase2/unit24-ab-report.json`;
it includes CSV hashes, all windows and each reversal pair.

After enabling the default and adding the build-availability guard, the release
build repeated both full corpus modes with the same 6,628 exact comparisons
and zero failures. A further 45 s live run using the default mask logged over
100,000 strict matches. The measured kernel header hash is unchanged; release
logs are `unit24-release-mode{2,1}.log` and
`analysis/vk-scene-p2-unit24-release-live.stderr.log`.
