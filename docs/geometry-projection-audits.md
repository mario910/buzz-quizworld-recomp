# Geometry projection and strict output audits

`BUZZ_GEOMETRY_NATIVE_RECIPROCAL` is experimental and defaults OFF. In ordinary
native job mode 1 it replaces the SPU reciprocal estimate/interpolation/
refinement with SIMD division for admitted finite W. This can change low
projection bits, quantized coordinates and retained triangle decisions.
It is not an exact culling policy.

## Observed boundary case

A live startup job exposed a case absent from the earlier 19508-job corpus.
The original SPU culler retained seven triangles; native division retained
six. The missing triangle used indices 5, 8 and 9. The first differing byte
was in the logical index stream, rather than its aligned tail.

Two projected Y values differ by one float bit on the original path and
become equal with native division. The original area determinant is
`0xB8C00000` (about -0.00009155 in projected XY units); the relaxed result is
zero. Packed XY coordinates still agree in this case. This establishes a
thin-triangle decision difference; it does not establish a visible rendering
failure or prove the approximate path correct for every scene.

The default-OFF option remains subject to visible-output and progression
qualification. Passing the old captured corpus does not guarantee identical
triangle selection on other inputs.

## Strict policy

Paired native job mode 2 compares complete published DMA/atomic payloads.
Its host producer now requests the original reciprocal projection, even when
the build contains native reciprocal division. Both bounded and checked
projection use that strict choice. Ordinary mode 1 keeps the requested
experimental arithmetic. Modes also retain their separate vertex-padding
policy; strict mode never selects relaxed zero padding.

This repairs the startup strict-audit failure without weakening byte
comparison, skipping a comparison or changing ordinary mode-1 arithmetic.
A mode-2 success validates the strict producer policy and I/O contract; it
does not validate the approximate reciprocal policy used in mode 1.

## Verification

- The reduced numerical regression checks the original coordinates and
  determinant on Windows, Linux x86 and ARM64/QEMU, with both bounded and
  checked projection. It also checks the unchanged relaxed result, masked
  padded lanes and absence of context writes.
- The captured startup entry matches the original pinned SPU input, skinning
  and culling oracle: all 76 clip words, 21 logical indices and the complete
  aligned index payload agree on all three architectures.
- Existing mode-guard tests retain padding-only rejection in modes 0/2 and
  rejection of invalid logical palette indices, including zero weights.
- A separate locked Windows mode-2 run reaches the animated menu and logs at
  least 1515000 completed whole-job boundary/I/O comparisons without a
  difference. A bound, unsuspended counter read observes 1421163 completed
  checks, two skipped paired checks and zero failures. Jobs rejected before
  paired-check admission are not covered by that counter. This diagnostic
  run is not an FPS benchmark or a gameplay-progression test.

The standalone regression is `tests/geometry-strict-projection-test.cpp` and
runs in the offline CPU suite. Private captures and the original-code oracle
remain outside Git. QEMU results establish correctness, not Android speed.
