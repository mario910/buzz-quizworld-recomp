# Native image-41 kernels

Phase 2 is in progress. The phase 0/1 base is commit `719f325`. New kernels
use strict state checks; the existing Stage F exception remains unchanged.

## Runtime controls

`BUZZ_GEOMETRY=0` forces the lift, `1` runs selected native kernels and `2`
compares every handled invocation against the lifted reference. The reference
state is retained in checked mode. `BUZZ_GEOMETRY_KERNELS` is the function bit
mask; its current default enables bit 0 when that implementation is built.
Bit 0 selects the coupled unit-vector
decoder below; bit 1 selects validated skinning and remains default-off because
its scene measurements are mixed.
Bit 2 selects the halfword encoder and remains default-off during validation.
Other function IDs are reserved while their implementations
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

## Bit 1: skinning (`0xB880/0xB934/0xB940`)

The entry is `0xB880`; the exits are `0xB4C0` and `0xBB58`. The region blends
three float4 palette rows using four interleaved byte weight/index pairs per
vertex, stores the resulting matrix and transforms the position in place.
There are no DMA, channels or nested calls in this region. The disabled
wrapper runs only the original setup and preserves its block continuation.

`native_geometry_skin.h` processes two vertices per AVX2 batch, retaining
ordered SPU FM/FMA arithmetic, the RTZ 1/255 coefficient, cyclic component
order and the matrix W fields. It leaves the final pair to the original
pipeline and reconstructs its setup pointers, including speculative loads
and every temporary register. A pair with both byte weights zero omits that
influence; the SPU operations produce canonical positive zero for it.

The guarded path requires an even count of at least four vertices, canonical
constants, aligned positions/weights/matrices/palette and bounded private LS.
Position and matrix outputs must be separate from each other and from the
palette/weight sources. It scans all palette indices before writing.
Debug probes, external LS, pending interrupts and unsupported shapes fall
back before any mutation. The final reference pair restores exact control
state and continuation, including both exits.

`tests/scripts/test-geometry-skin.ps1` checks 440 randomized full-state cases,
counts from 4 to 2048, arbitrary palette bit patterns, all-zero, one-hot,
sparse and dense influences, both exit continuations, and ten unchanged-state
rejection cases. The
optional private `pc_B940.bin` sample measures approximately 236 ns lifted
versus 157 ns native; it resets the in-place positions each iteration.

Both strict checked replay and separate native execution match all 19,508
image-41 jobs and 58,195 transfers, with zero input divergences or failures.
The skin checker compares 5,700 groups exactly; 413 unsupported groups use
the reference. Private evidence: `phase2/skin-guard-mode{2,1}.log` and
`phase2/skin-build.json` under `analysis/native-geometry/`. The final build's
45 s live check logged at least 100,000 strict matches and no differences;
the earlier 90 s check logged at least 700,000. A/B round introduction dumps
with native execution enabled and disabled both look normal. Logs are
`analysis/vk-scene-p2-skin-guard-live.stderr.log`; the kernel header hash
remained unchanged throughout the final corpus/live/A/B checks.
The same frozen build also rendered a normal menu in actual native mode
(mask 3, separate 45 s run); its log is
`analysis/vk-scene-p2-skin-native-menu.stderr.log` and the private renderer
image is `phase2/skin-native-menu.png`.

The same frozen EXE ran reference/native/native/reference in both scenes,
with bit 0 enabled throughout (masks 1 and 3). Menu runs yield
32.00 / 31.86 / 31.87 / 31.96 FPS: means 31.98 -> 31.87 (-0.36%), with
pair changes -0.44% and -0.28%. Round runs yield 39.59 / 40.67 / 39.81 /
39.41 FPS: means 39.50 -> 40.24 (+1.88%), with pair gains +2.74% and
+1.00%. Windows and settings match the earlier decoder methodology; each
run starts from an identical private save copy. The report is
`analysis/native-geometry/phase2/skin-ab-report.json` and the source set is
`phase2/p2-skin-ab2/progress.jsonl`. The earlier `p2-skin-ab` set was aborted
before completion while adding the weight-alignment guard and is not used.

Bit 1 stays **default-off** because the round improvement is accompanied by
a small menu regression. `BUZZ_GEOMETRY_KERNELS=3` enables it together with
the default decoder for experiments; `BUZZ_GEOMETRY=2` checks both strictly.
This completes the skinning implementation and correctness gates without
claiming an improvement in both scenes.

## Bit 2: halfword encoder (`0x4AB0`)

The entry is `0x4AB0`; the exit is `0x4A9C`. This pure loop writes the pending
four packed vertices through a byte shuffle mask, prefetches four float4
records, converts them to halfwords and advances the source and destination
routes. It also writes a shared sentinel after the useful source range.

`native_geometry_half.h` converts eight components per AVX2 batch using
integer operations. It preserves SPU truncation, saturation to magnitude
`0x7BFF`, signed normal values and flushing underflow to positive zero.
It does not substitute an IEEE half conversion. Pending vectors and all
pointer words advance exactly; the original final iteration restores every
temporary register, final speculative loads, sentinel bytes and continuation.
The destination shuffle retains all 256 selector meanings.

The fast path requires six canonical constant registers, positive aligned
source stride/record offsets and bounded private LS. It checks every routed
destination quadword, including sentinels, for bounds and overlap with all
source streams before writing. Unsupported layouts, debug probes, external
LS and pending interrupts take the lift without mutation.

`tests/scripts/test-geometry-half.ps1` checks 440 full states with random bit
patterns, explicit FP boundaries, arbitrary destination masks, 2..512 loop
iterations and eleven unchanged-state rejection cases. On the private
`pc_4AB0.bin` menu sample, the local microbenchmark measures approximately
122 ns lifted versus 85 ns native.

Both checked and actual-native full corpus modes pass all 19,508 image-41
jobs / 58,195 transfers, with zero divergences or failures. All 18,776 encoder
groups are handled and match strictly, with no fallback. The checked replay
also repeats the existing unit-vector and skinning checks with all three
bits selected. Private logs are `phase2/half-mode{2,1}.log`; `half-build.json`
binds the frozen executable and source hashes.

The 60 s live run (mask 5, keeping skinning off) logged at least 500,000 exact
encoder comparisons and no differences:
`analysis/vk-scene-p2-half-live.stderr.log`. Renderer and reversed-scene FPS
gates remain pending. Bit 2 stays default-off; this is an experimental
implementation checkpoint, not a completed performance claim.
