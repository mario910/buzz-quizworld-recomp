# PC game benchmark of mobile geometry and SPURS at 12840be

Four scripted runs completed the Polish EU 1.00 solo challenge *Wyzwanie nad krawędzią* on Windows. Both pairs gained FPS during questions. Menu FPS moved in opposite directions across the pairs.

| Scene and driver window | FPS off → on | Process CPU off → on | CPU ms/frame off → on | p95 frame interval off → on |
| --- | ---: | ---: | ---: | ---: |
| Studio menu, 55–79 s | 106.77 → 104.58 | 24.19% → 24.11% | 54.37 → 56.19 | 13.13 → 15.04 ms |
| Quiz, 204–242 s | 114.04 → 125.95 | 22.05% → 22.56% | 46.41 → 42.99 | 12.19 → 10.94 ms |
| Longer quiz window, 210–295 s | 110.36 → 126.03 | 22.33% → 22.45% | 48.56 → 42.76 | 12.63 → 11.43 ms |

The first quiz window was selected before comparing timings. The longer overlapping window checks whether the result holds across additional questions and score transitions. Average quiz FPS increased by 10.4% and 14.2% respectively. With the cap disabled, the game spent slightly more process CPU time per second and 7.4–11.9% less CPU time per frame. The table gives each run equal weight. CPU percentage uses the machine's full 24 logical processors as 100%. The p95 column averages the two per-run 95th percentiles.

| Run order | Build | Menu FPS | First quiz window FPS | Longer quiz window FPS | Longer window CPU ms/frame | Replay prompt observed |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | On | 90.86 | 126.73 | 126.25 | 42.46 | 356 s |
| 2 | Off | 106.40 | 113.26 | 109.92 | 48.70 | 332 s |
| 3 | Off | 107.14 | 114.82 | 110.81 | 48.41 | 344 s |
| 4 | On | 118.30 | 125.18 | 125.82 | 43.06 | 319 s |

## What the builds exercise

Both executables reuse the same existing desktop Vulkan objects for the renderer and the remaining guest code. The experiment rebuilds the geometry job flow from `12840be` and the SDK SPURS job translation unit. Both retain the desktop native host pipeline, scalar gather, host commands, vertex padding and x86 floating-point control scope.

The on build enables bounded skinning, coarse clipping, cull compaction, deferred clipping, lazy attributes, index caching, batched and sequential index decoding, and the portable mobile geometry kernels. Private benchmark headers enable the source's x86-compatible arithmetic for bounded transforms, reciprocal interpolation, skin normalization and packed direction decoding. The SPURS code uses the mobile image-41 upload, clear, 21-entry DMA and descriptor/context paths. The off build disables the nine geometry switches and uses the original SDK staging code. Remaining ARM-only NEON operations and Android graphics policy are outside this experiment. These two Windows executables measure the group as a whole.

All five candidate correctness checks passed after the timed runs. These cover bounded position and skin output, deferred projection, the packed direction decoder against the original SPU output for 57,344 vertices, and all 32,768 SPURS DMA copy lengths.

## Conditions and verification

- Ryzen 9 7900, 12 cores and 24 threads, GeForce RTX 4060 Ti, Vulkan at 1280 × 720, 100% render scale, mailbox presentation and no frame cap. Renderer logs agree on all four runs, including two draw contexts, draw batch size 512 and 1,000 µs worker polling.
- The driver used the project's input mailbox, chose Play, navigated to solo challenges, selected Adam and cycled the four answer buttons. Screenshots show questions, answer judgments, score changes and the post-game replay prompt in every run. Random question selection produced different questions and round lengths.
- Each run copied the same player save and warm shader cache. The timed series used base-disc questions. A pilot input file initially lacked its terminating newline. Two subsequent diagnostic runs reached the character screen with the installed extra packs. All three were excluded. The successful route also added a ready confirmation, so the trials do not isolate what caused the earlier stall.
- FPS uses completed Vulkan presentation deltas divided by measured wall time. CPU uses the game's total Windows process time sampled at most 0.5 s apart and interpolated at the frame boundaries. Buffered renderer timestamps were aligned to the driver's clock using phase markers. Every included frame had presentation delta one.
- The analysis removes a one-second margin on either side of each rounded window-capture timestamp and the interval 242–248 s around the one-shot GPU readback. This leaves about 24 s of studio, 32 s of the first quiz window and 67 s of the longer quiz window per run. Android hardware FPS and power use require separate measurements.

## Measurement files

The [per-run measurements](pc-mobile-cpu-12840be.csv) give frame counts, measured seconds, FPS, CPU use and frame intervals. The benchmark workstation retains the scripts, raw samples, logs and screenshots under `analysis/pc-ab-12840be`. The Windows EXE SHA-256 values are `d141f329ccbef0bc6db0acba7ca09f7b76ff3a60fffa2a1dd48867c7c0974e39` (off) and `44030928ccb267d4f6cfcedfdb8c3fb76fb23f70c5db5128a340f375868cc6fe` (on).
