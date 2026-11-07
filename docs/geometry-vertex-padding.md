# Unindexed vertex alignment bytes

`BUZZ_GEOMETRY_ZERO_VERTEX_PADDING` is an experimental, default-OFF option for
the full native host geometry producer in job mode 1. It zeroes only the
0–12 alignment bytes after the last logical vertex. Every logical attribute,
triangle index, draw count, allocation size and ordered DMA operation retains
the existing path. Paired strict job mode 2 uses the original padding policy,
including its previous fallback decisions and complete byte payloads.

The original formatter evaluates an extra position to fill some aligned
output tails. A weighted padding vertex can contain an out-of-palette index;
the exact host tail helper rejects it because reproducing its incidental LS
scratch reads requires the legacy flow. That fallback repeats work already
performed by host input capture, skinning and culling.

The relaxed policy does not evaluate that unused position. It requires the
admitted dense vertex layout, matching logical vertex counts, at most 4096
vertices, a stride no larger than 64, aligned output size and at most 12 tail
bytes. The producer still validates every decoded index against the logical
vertex count, including for whole-object clip rejection. Logical palette
indices remain checked even for zero-weight influences. Output/IO spans and
allocation bounds include the full aligned payload. No padding is inserted
between logical vertices or used as a substitute for a missing attribute.

## Build controls

- Windows/Linux CMake: `-DBUZZ_GEOMETRY_ZERO_VERTEX_PADDING=ON`.
- Windows helper: `-NativeGeometry -GeometryZeroVertexPadding` with the
  existing host geometry arguments. Omitting the new switch resets it OFF.
- Android: `-PbuzzGeometryZeroVertexPadding=true` with both
  `buzzNeonGeometryDriver=true` and `buzzNeonHostGeometry=true`. The property
  defaults false; reference29 explicitly keeps it OFF.

## Verification

The original pinned SPU formatter is the oracle for all logical vertex bytes;
the original culler and serializer remain the oracle for indices. Native
reciprocal projection may differ in intermediate clip bits, so this test
compares retained indices and emitted logical vertices instead of requiring
those intermediate bits to match. Other clip-oracle tests keep their strict
checks. Zero-filled tail bytes are checked separately with protected ends,
prefix canaries and short-capacity rejection.

`tests/geometry-zero-vertex-padding-test.cpp` covers the complete private
19508-job corpus and the existing numerical corner cases. The real visible
outputs include 7167 jobs and 35655712 identical logical vertex bytes, with
632 previous padding-tail rejections. The composed producer admits 584 of
those newly supported cases and retains original indices and logical vertex
output on 6867 visible jobs. Only unindexed alignment bytes differ.

Windows, Linux x86 and ARM64/QEMU comparisons pass. The mode-guard fixture
checks native mode 1, exact behavior in paired mode 2, and retained rejection
of invalid logical palette data. The existing malformed-index test also
checks both eager/lazy zero-padding producers. Both new fixtures are included
in the offline CPU suite's `--corpus` mode.

## PC performance qualification

Six matched 72-second runs retain the same locked Windows session, immutable
reference14, private save seed, script, warmed shaders, mailbox mode, two
contexts, 1024x576 client and 1280x720 render extent. Effective settings,
EXE/map identities and presentation checks pass. All six 35-second Vulkan
readbacks show the intended menu. No build, correctness test, profiler or
second game overlaps the timed FPS/CPU windows.

| Build | Pooled heavy 45–60s FPS | Pooled 45–65s FPS |
|---|---:|---:|
| Same immutable reference14 | 37.9743 | 39.4549 |
| Previous native build, 1024-cell cache | 42.1165 | 44.0238 |
| Same pipeline, zero vertex alignment | 44.0901 | 45.7597 |

The heavy-window improvement is about 1.97 FPS. Candidate repeats are
43.7190/44.4613 FPS; their 45–65s P99 is 28.940/28.122 ms. The longer-window
average above 45 FPS is **not stable 45 FPS**.

Persistent-handle CPU counters during 45–65s, without suspension or timed
thread discovery, measure summed CPU work across threads:

| Measurement | Previous | Zero alignment |
|---|---:|---:|
| Process CPU ms/frame | 123.4916 | 117.5205 |
| Process cycles/frame | 453.009 million | 436.972 million |
| SPURS pool CPU ms/frame | 55.7301 | 51.4344 |
| Uncapped process busy cores | 5.4366 | 5.3767 |

This is 4.84% less process CPU time and 7.71% less SPURS pool CPU time per
frame in this workload. CPU milliseconds are not frame wall time or measured
electrical power. Android hardware improvement remains unproven.

Private `zero-padding-*` artifacts under `analysis/pc45-image39/` retain the
raw frames, CPU counters, guards and readbacks. Frozen EXE SHA256 values are:

- reference14: `d66601b4c612eb4006597d285e9d247ed538692d269cadb1448219179fb903fb`
- previous: `9bc03592516babdbccbd693f5594ca2589faa9c8aca3b2a945252612870c9017`
- zero alignment: `8d6bb86001d1c2a57873cdced964b1da3a3f96507961ab93dc544e8546ef1f98`

A separate longer round fixture reaches the Adam character screen but does
not establish question/answer/scoring progression. No complete-gameplay or
Android-device qualification is claimed. QEMU verifies CPU correctness only.
