# Exact GPU unit-vector decoding on Steam Deck

A standalone Vulkan compute decoder matched the existing CPU implementation,
but its complete roundtrip was slower at every tested batch size. It remains a
private feasibility experiment. No game code, optimization default or installed
executable changed, and this does not establish progress to stable 30 FPS.

## Arithmetic qualification

The fixture used the actual AMD VANGOGH GPU and the game's existing HLSL-to-SPIR-V
compiler. A private backend copy requested Vulkan 1.2 and checked the device's
32-bit round-toward-zero, flush-to-zero and signed-zero preservation support.
The shader retained the SPU reciprocal-root estimate table and integer
interpolation, explicit arithmetic order, zero repair and selector behavior.
Zero remainder is handled explicitly; the bounded nonzero remainder permits
ordinary finite arithmetic.

The compiled module declares the corresponding
[SPIR-V float controls](https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_float_controls.html).
The fixture rejects RelaxedPrecision and floating division, requires exactly
two explicit FMA instructions and at least five separate arithmetic operations,
adds NoContraction to the latter, and validates against Vulkan 1.2 with
SPIRV-Tools before creating a pipeline. An initial attempt failed these checks:
passing the decoder call directly to HLSL Store4 duplicated the calculation
three times. Materializing its result first fixed that duplication. Both the
failed disassembly and the corrected modules are retained.

Each successful configuration compared all 16,777,216 24-bit packed words and
1,048,576 arbitrary high-bit words: 71,303,168 output words, all bit-exact. This
includes selectors 0–15, beyond the admitted game's 0–11. The independent CPU
oracle used the unchanged root decoder headers, AVX2 and scoped RTZ/FTZ/DAZ.
The local-table and buffer-table modules both passed; the latter no longer
contains the per-invocation constant-array variable.

## Submission and memory costs

Synthetic timings used 64, 256, 1,024, 4,096, 16,384 and 65,536 inputs, selectors
0–11, and CPU/GPU/GPU/CPU order with 32 calls per arm. CPU time includes decoding
and AoS output assembly. GPU time includes mapped input fill, command recording,
submission, completion wait and copying every output byte to CPU memory.
Allocation, shader compilation and output comparisons are outside the timers.
Every timed output was subsequently compared. These are small standalone
experiments without concurrent game load, not gameplay or throughput claims.

The first storage buffer selected uncached coherent memory (type 2, flags 6).
Selecting cached coherent memory (type 5, flags e) in the same fixture binary
reduced the mean 65,536-input GPU roundtrip from 10.074 ms to 0.421 ms; the CPU
means were 0.338 and 0.327 ms respectively. Both configurations passed the full
arithmetic comparison. This identifies a cost in the fixture's memory choice,
not a demonstrated allocation defect in the live renderer.

A subsequent same-binary comparison used cached memory in both configurations.
One shader retained the private 64-entry estimate array. The other loaded the
same immutable table from a disjoint 256-byte suffix of the storage buffer;
both allocated that suffix. Mean roundtrip microseconds:

| Inputs | CPU, local-table run | GPU, local table | CPU, buffer-table run | GPU, buffer table |
| --- | ---: | ---: | ---: | ---: |
| 64 | 0.351 | 81.823 | 0.419 | 80.367 |
| 256 | 1.502 | 50.055 | 1.724 | 51.767 |
| 1,024 | 5.171 | 53.618 | 6.975 | 63.690 |
| 4,096 | 20.287 | 74.772 | 20.126 | 74.029 |
| 16,384 | 82.506 | 144.139 | 80.466 | 140.551 |
| 65,536 | 330.201 | 408.280 | 337.346 | 378.205 |

The buffer-table variant still loses to CPU decoding. At 65,536 inputs its
mean components were approximately 46.6 us filling input, 247.2 us recording,
submitting and waiting, and 84.4 us copying output. The short runs vary enough
that differences between shader variants should not be treated as established
gains. Per-job GPU offload is not justified by these results.

## Limits and retained state

This checks one arithmetic stage, not complete geometry jobs or live frames.
Larger offload would need evidence that enough independent work is available
and that staging preserves DMA read/write order, ownership, publication,
notifications and guest-visible output. Adjacent job descriptors alone do not
prove that their payloads can safely be captured or executed together.

Private artifacts are under
`/home/deck/buzz/performance/optimization/codex-gpu-unit-rtz/`: source, build
driver, independent oracle, original/patched SPIR-V, full comparison logs,
memory-selection logs, per-arm timing breakdowns and `timing-summary.json`.
`table-local.log` and `table-buffer.log` are the final completed comparison;
`cached-0.log` and `cached-1.log` preserve the memory experiment. No game-derived
binary or generated implementation is committed.

The prepared game remains the restored qualified reference
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.
The [combined CPU decoder experiment](steam-deck-unit-decoder.md) likewise
did not establish a net FPS gain. Stable 30 FPS remains unmet.
