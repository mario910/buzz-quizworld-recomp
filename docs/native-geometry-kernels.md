# Retained image-41 kernels

These per-function kernels serve lifted fallback and diagnostic routes. The
active [host pipeline](native-jobs.md) replaces much of the same work as a
whole and does not reproduce internal register banks or call frames.

## Runtime controls

`BUZZ_GEOMETRY=0/1/2` selects lifted reference / enabled native groups /
full-state comparison retaining the reference result. Unsupported inputs use
the reference before native writes. The implemented mask is 6015 (`0x177F`);
the default is 827 (`0x33B`), intersected with the compiled mask. Bits 7 and 11
are reserved because their blocks share groups 0 and 9.

| Bit | Group | Default |
| --- | --- | --- |
| 0 | Unit vectors `9148/92A0` | On |
| 1 | Skinning `B880/B934/B940` | On |
| 2 | Halfword encoder `4AB0` | Off |
| 3 | Triangle stitching `7A90` | On |
| 4 | Packed output `5008/51E0/51C8` | On |
| 5 | Halfword decoder `CAD8/CB78` | On |
| 6 | Index expansion `78A8` | Off |
| 8 | Raw float3 input `97A8` | On |
| 9 | Raw float3 output `46FC/4750/4808/47F0` | On |
| 10 | Staged-list pointers `0988/09E0` | Off |
| 12 | BE word fill `28F0` | Off |

The four off groups passed correctness checks but did not justify default-on
in their combined scene/CPU measurements. Their mask does not disable the
corresponding work in the host pipeline. Comparison mode checks real entry
ownership before cloning LS, rejects unavailable mask bits and requires
selected kernels to make comparisons when exercised.

The version-bound code generators and harnesses live in `tools/codegen/` and
`tests/scripts/`. Derived reference code and snapshots remain private.


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

## Bit 5: halfword decoder (`0xCAD8/0xCB78`)

The entry is `0xCAD8`; the region returns through the link register (the
reviewed call site returns to `0x83C8`). It covers all remaining columns of
the attribute, not just one pipelined `0xCB78` loop. There are no DMA/channel
operations or nested calls in the region.

`native_geometry_half_decode.h` decodes pairs of float4 records with AVX2
integer operations. It preserves the exact table shuffle's repeated first
quadword byte in the low output bits, arithmetic sign extension, magnitude
test and bias addition. An IEEE/F16C conversion would lose those bits and
change zero/subnormal behavior. Stores retain the original 1,2,0,3 order,
including aliased sentinels. The original final column reconstructs all
temporary registers, padded reads and the return continuation.

The adapter consumes queued trampolines before directly executing each
reference block. A host-frame `SPU_RET` intentionally leaves the trampoline
untouched; the adapter must enter it with an empty queue, just like the
normal dispatcher. Tests explicitly verify empty host returns and indirect
returns at depth zero, in addition to comparing the full architectural state.

The guarded path requires seven canonical registers, the reviewed layout of
at most 16 columns, paired/aligned source quadwords, bounded input/output,
uniform loop strides/limits and nonoverlapping input/output ranges. It
rejects wrapped column indices before addition. Unsupported layouts, debug
probes, external LS and pending interrupts use the lift before any mutation.

## Bit 10: staged-list pointers (`0x0988/0x09E0`)

The entry is `0x0988`; the exit is `0x08F0`. This pure region builds the input
slot pointer table from staged eight-byte size/EA records. DMA issue happens
outside it, so function comparison does not repeat external side effects.

`native_geometry_pointers.h` reads the BE size field's low 15 bits and the
EA alignment nibble, emits one BE pointer word per entry, then advances the
logical LS pointer by the size rounded to 16. Logical pointer and counter
arithmetic retain 32-bit wrap. The original final entry reconstructs every
temporary register and the exact continuation. Direct block calls consume
their queued trampoline just like normal dispatch.

The guarded path requires three canonical masks, 2..128 remaining entries,
word-aligned source/output, bounded private LS and no overlap between output
words and the size/EA records. Output may begin at the documented `0xE500`
slot table. Bounds use 64-bit arithmetic before pointer construction, so
wrapped source addresses are rejected. Debug probes, external LS, pending
interrupts and unsupported layouts fall back before any mutation.

## Bit 12: BE word fill (`0x28F0`)

The pure region enters at `0x28F0` and exits at `0x2910`. The native prefix
fills all but the final word with the BE representation of `r9.word0` using
AVX2 stores and a word tail. It advances every lane of the pointer/counter,
then executes the original final iteration to restore temporary registers,
quadword padding and continuation exactly. Counter subtraction retains
32-bit wrap; bounds are checked in 64 bits before any mutation.

The guard requires at least four remaining words, a four-byte aligned,
bounded output at or above `0xE680`, private LS, and no pending interrupt or
debug probe. Smaller or unsupported fills keep the lift. Bit 12 is opt-in
and remains off after its mixed scene/CPU results.

## Bit 8: raw float3 decoder (`0x97A8`)

This pure loop exits at `0x9498`. The pending first group comes from three
entry registers; subsequent groups read packed BE float3 records and append
the exact `1.0` W bits. AVX2 handles two vertices per vector without changing
their XYZ bits, including any non-IEEE patterns. The final lifted iteration
restores all temporary GPRs and speculative prefetch state. The guard checks
five canonical constants, at least four groups, 16-byte alignment, bounded
private LS and disjoint source/output ranges. Output streams may alias each
other; store order is retained. Unsupported inputs return before mutation.
The test compares 440 full states up to 512 groups, aliases and random XYZ
bits, then sixteen unchanged-state rejections. Full

## Bit 9: raw float3 output (`0x46FC/4750/4808/47F0`)

`4750` and `4808` are the straddled/aligned arms of the same remaining-column
operation, entered at `46FC` and exiting at `4610`. They share bit 9 rather
than two independent replacements. The prefix copies twelve raw bytes into
each field, preserving the nominal field offset even when routing to the
sentinel. The guard proves bounded input/output and separates input, normal
output and sentinel ranges. It accepts supported interleaved strides,
unaligned input, mixed sentinel routing and overlapping output groups.

The original final column reconstructs temporary registers. One additional
carry matters: aligned arm `4808` does not write `r12`, so the prefix retains
the padded load from the most recent straddled column. The adapter consumes
the queued continuation before every direct block call.

## Bit 3: triangle stitching (`0x7A90`)

This pure loop exits at `0x2738`, including its original stack-register
restores. The prefix retains eighteen loop-carried GPRs in locals, prepares
only used entries of the two shuffle-control tables, and reconstructs three
output quadwords per iteration. Every selector class is supported. Input
prefetches remain before output stores, preserving in-place operation.
The lifted final iteration restores all temporary GPRs, stack registers and
the continuation exactly.

Five canonical masks and bounded private LS are required. The code-stream
quadwords cannot overlap output; the control tables remain below `0xE680`.
After the first input step, `6*population + 2*(8-population)` is at most 48,
so one conservative interval check bounds all speculative reads. This
replaces an expensive second scan of the code stream. Initial table indices,
population/adjustment, output step and iteration count are checked before
mutation. Unsupported/debug/interrupt shapes retain the lift. Bit 3 is now
on after all scene gates and repeated cycle/CPU-time savings.

## Bit 6: index expansion (`0x78A8`)

The prefix keeps thirteen live GPRs in locals and predecodes fixed shuffle
controls once. All control-byte values and logical input-address wrap are
supported. Reads use the exact SPU quadword alignment/wrap helper; output
alone is restricted to aligned, bounded private data at or above `0xE680`.
The initial output step must be 0 or 32 and count must be 4..4096. Input
reads remain before stores, including code-stream/input/output aliases.

One important carry is `r10`: it contains the population count of the masked
code, rather than the earlier shuffled-code temporary. The lifted final
iteration reconstructs every temporary and the post-loop continuation
(`0x79A8` or `0x7DCC`).

## Bit 4: packed 11/11/10 output (`0x5008/51E0/51C8`)

The replacement covers remaining columns, entered at `0x5008` and exiting
at `0x4610`. AVX2 pairs preserve SPU RTZ FMA followed by CFLTS scale 31 and
the exact 11/11/10 bit packing. Exponent-255 lanes use the scalar SPU repair.
The guard requires ten canonical controls, word-aligned fields, supported
strides and separate bounded input, normal output and sentinel ranges.
Other alignments retain the `0x50C8` path through the reference group.

The value comes from source group `g`, while its routing comparison uses
the cursor for `g+1`. Bounds include the final speculative prefetch. Stores
retain the nominal field's low nibble even on a sentinel route. The lifted
final column reconstructs all temporary state; the prefix advances every
lane of `r44/r47`. Direct reference calls consume the queued continuation
before each block, matching normal dispatch.
