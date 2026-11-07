# RSX preparation study after native job completion

The accepted experimental PC bundle remains the native job-completion EXE
`2974056c77faadfef4bf23df187a46693bd65467aa302697fa5a7845202d35f3`,
with `BUZZ_SPU_JOB_RETURN=1` and `BUZZ_GEOMETRY_CHANNEL_SETUP=1`.
The immutable reference14 remains
`d66601b4c612eb4006597d285e9d247ed538692d269cadb1448219179fb903fb`.
See [native job completion](spu-job-return.md) for its implementation and
qualification. The stable 45 FPS gameplay objective remains open.

## What the new profile measures

A private renderer probe sampled one in sixteen completed draws using
`QueryThreadCycleTime`, with a calibrated single-read cost. It split geometry
into layout, reference construction, remapping, plan preparation and conversion.
The last fifteen busy menu reports identified reference construction and
conversion as similarly expensive: approximately 9.2% and 9.6% of the measured
RSX thread cycles. Layout and plan preparation were approximately 2.7% and 1.8%.
The backend draw callback was approximately 24.7%, and pipeline preparation 9.2%.
The workload issued roughly 2,700 small draws per presented frame.

These are diagnostic estimates from one thread, not whole-process CPU shares,
an exact 45–60 s window or FPS evidence. The enclosing geometry counter includes
the extra nested detail-clock calls: its raw 39.4% estimate is inflated and must
not be quoted as geometry's exact CPU share. Nested and enclosing measurements
must not be added. Calibration reduces, but does not eliminate, instrumentation
cost and sampling uncertainty. An independent eight-second unwind sample of the
accepted EXE completed 105 rounds with no resume failures; its snapshots are
wall-clock observations, not weighted CPU attribution.

## Rejected renderer index-cache prototype

A private four-way, 128-entry cache reused first-use u16 reference lists and
occurrence maps after comparing every source byte. It admitted at most 1,024
indices per group, bounded owned payload storage to 1.75 MiB, preserved span
ordering and restored the current base index in returned references. A sampled
fingerprint selected candidates; it never replaced the full byte comparison.

The first two diagnostic variants had only approximately 28–30% total hits.
Removing the base from the key made little difference. A third variant added
middle-index samples to reduce fingerprint clustering. Its final six-run test
used the accepted EXE as the incremental control and reference14 at both ends:
reference / accepted / candidate / candidate / accepted / reference. Each run
lasted 72 seconds in the same locked Windows session, with a 1024x576 client,
1280x720 rendering, fixed save/input, warmed shaders and unchanged wait policy.
Both current arms kept native job completion ON. No compilation, correctness
testing, sampling or second game overlapped the timed windows. Cache statistics
were OFF in these measurements.

| Metric | Accepted EXE | Private candidate |
|---|---:|---:|
| Heavier 45–60 s menu interval, pooled FPS | 45.935 | 44.131 |
| 45–65 s menu interval, pooled FPS | 47.712 | 46.363 |
| P99 interval, 45–65 s, two repeats (ms) | 27.995 / 27.071 | 32.353 / 28.025 |
| Process CPU time per frame, pooled (ms) | 113.361 | 118.081 |
| RSX thread CPU time per frame, pooled (ms) | 15.306 | 16.603 |

Reference14 measured 37.141 / 37.820 FPS in the heavier interval (pooled 37.480).
Accepted repeats were 45.458 / 46.411; candidate repeats 42.748 / 45.513.
The spread does not establish a universal regression percentage, but this
candidate demonstrated neither an FPS gain nor CPU savings. Pooled process
CPU/frame increased 4.16% and RSX CPU/frame 8.47%. All six readbacks showed the
same menu without a visible regression. These results do not measure gameplay
or Android performance. The candidate EXE was
`647ced8088f80ba066e4f4e219fc14ac25c04da7f0eeb5ed5cf0030ab7cf07bf`.

The prototype is rejected and has no retained production option or Android
property. Its source, binaries and raw diagnostics remain private research
artifacts. The existing geometry-job index cache is a separate retained feature.

## Cache-fill coherence lesson

Sequential mutation, forced fingerprint collisions, relocated identical input,
allocation failure, SDK interleaving and guarded-tail tests passed on Windows,
Linux x86 and ARM64/QEMU. A later deterministic test exposed another case:
the source changed during cache-storage allocation, after output computation.
Copying that newer source into the entry associated it with the older output;
a subsequent full-byte-verified hit could therefore return the wrong mapping.
This was reproduced offline; no live-game occurrence was established.

Full hit verification alone does not prove fill coherence. A future cache must
derive its result from the same owned snapshot it stores, or establish an
equivalent producer/version contract. The rejected prototype is not promoted
on the strength of its earlier sequential tests. ARM/QEMU checks establish
correctness coverage only, not phone performance.

The remaining measured work supports investigating conversion, backend draw
submission and pipeline preparation. Further caching needs a demonstrated
reuse rate and coherent fill contract before its complexity is retained.
