# Skinning preparation and a fresh Deck menu corpus

Two exact preparation prototypes passed captured-output comparisons, but
neither demonstrated a substantial kernel improvement. Neither was integrated
into the game. A new current-reference menu corpus adds direct coverage of the
scene used in clean FPS comparisons. Stable 30 FPS remains unmet.

## Candidates and checks

The recipe prototype reuses the skinning recipe already validated at the
start of ordinary job preparation. Attribute capture writes owned host buffers
without changing the local-store inputs. The original skinning stage then
redundantly reads and validates the same recipe. The prototype removes that
second read only within this immutable preparation interval, including the
deferred attribute phases. It retains the original self-validating call after
mutable blend execution. Readiness checks, failure behavior and arithmetic
remain unchanged; no recipe or guest payload is cached across jobs.

The palette prototype is independent. AVX2 skinning currently fills both a
coefficient-major matrix array and a matrix-major row array, while its vector
path reads the rows. The prototype omits coefficient-array stores on that
path and reads strict vertex-padding coefficients from the row array too.
It retains the original workspace layout, all palette/index admission and
arithmetic, and the non-AVX2 path. The unused array is filled with `0xa5` before
candidate checks to expose accidental reads from it.

ASan/UBSan fixtures compare original and candidate complete preparation:
acceptance and rejection reasons, readiness/pending flags, active attribute
blocks, padded clip records, index state and every emitted vertex/index byte.
They also verify that entry registers and local store remain unchanged.
Ordinary and deferred preparation both pass. The palette fixture additionally
compares strict padding, preserving its existing rejections. These are
preparation/output checks, not complete job DMA/publication qualification.

| Corpus | Files | Supported preparation cases | Object | Weighted |
| --- | ---: | ---: | ---: | ---: |
| Menu, newly captured | 1,500 | 1,493 | 1,020 | 473 |
| Question | 1,500 | 1,349 | 911 | 438 |
| Reveal | 4,000 | 3,879 | 2,457 | 1,422 |

The excluded question/reveal cases are 146/109 mutable blend jobs and 5/12
unsupported storage-4 jobs. Menu excludes seven storage-4 jobs. All exclusions
match the original. There are no unskinned cases in these admitted sets.
Each corpus also passes 128 codec mutations rejected identically at admission.
Strict padding admits 1,259/1,076/3,150 menu/question/reveal cases and rejects
234/273/729 identically. The blend execution path is retained by source
construction but is not executed in these standalone fixtures; unexpected
guest/DMA calls abort.

The first recipe fixture stopped because its assertion expected the wrong
name for the weighted-codec rejection. Original and candidate had both
rejected it with the same reason. The corrected fixture records rejection
categories while retaining the equality and rejection assertions. That
initial failure log is preserved.

## Complete-preparation timings

Each corpus supplies up to 200 regularly sampled admitted cases, including
fully culled jobs: 200 menu, 193 question and 194 reveal. Each case runs 500
times in original/candidate/candidate/original order. Warm layout repeats one
case; streaming layout cycles through all retained cases. Timers cover complete
preparation, excluding capture loading and output comparisons. Both modes use
the same persistent workspace and produce equal checksums. Compilation uses
Clang 18, O3, AVX2/FMA and native RTZ with the original arithmetic guards;
these standalone binaries have no PGO. No other benchmark or game runs
concurrently.

Mean streaming ns/complete preparation, from separate experiments:

| Corpus | Recipe reference | Recipe reuse | Palette reference | Rows only |
| --- | ---: | ---: | ---: | ---: |
| Menu | 9,614.6 | 9,590.8 | 9,452.9 | 9,399.5 |
| Question | 13,232.0 | 13,159.7 | 13,128.5 | 13,098.4 |
| Reveal | 11,418.1 | 11,431.2 | 11,386.8 | 11,349.0 |

Recipe reuse reduces these means by 0.25%/0.55% in menu/question and regresses
0.11% in reveal. Rows-only differences are 0.56%/0.23%/0.33%; its warm
question/reveal results regress 0.52%/0.24%. These small differences do not
justify live integration or establish an FPS gain. Separate experiment
baselines must not be pooled, nor their differences added.

## Menu capture and replay

The frozen reference `630007cb…` ran a fresh 90-second menu at 1280×720 and
100% scale with the usual uncapped mailbox/two-context profile and image-36
search. Its existing recorder sampled every 1,379th image-41 job after second
40, stopping at 1,500 jobs. Sidecars span 40.000–73.517 seconds relative to
corpus initialization. Frames progressed to 89.814 seconds; the second-80
capture was inspected and intact. Recording overhead means this run is
excluded from clean FPS evidence. Systematic sampling across concurrent
workers is not proof of an unbiased workload sample.

The 1,500 files contain 7,779 transfer records. Replaying them on the same
reference with three repetitions reported 1,500 exact files, zero concurrent
input changes and zero failures: 4,500 executions. Whole-job replay classified
266 files as fully culled and 1,234 as drawn. The standalone admitted subset
contains 264 fully culled cases; its narrower admission is a different scope.

Among admitted jobs, mean vertices are 277 menu, 309 question and 320 reveal;
mean staged input sizes are 9,562, 11,086 and 10,712 bytes. Object/weighted
proportions are similar. This supports keeping menu coverage in future
experiments, without attributing earlier flat FPS to a large family mismatch.

## Follow-up: fused object capture and clip-parameter reuse

Two further private prototypes retained the original arithmetic. Neither was
integrated into the game.

The fused object path decodes each attribute block into a local value,
transforms it and stores it once, removing the intermediate untransformed
buffer write. Weighted, blend and deferred paths remain unchanged. The clip
cache instead keeps one derived parameter set per thread: every call retains
the original metadata and codec validation, then compares all 128 viewport
bytes and all 64 default-matrix bytes before reuse. It is restricted to masked
RTZ/FTZ/DAZ, and reproduces the original host floating-point sticky flags.

Both passed ASan/UBSan comparisons of all supported menu/question/reveal jobs,
including eager/deferred host blocks, clip records, emitted bytes, unchanged
guest state and 128 codec mutations per corpus. Admission and exclusions match
the earlier recipe fixture. A separate clip-cache test passed 9,342 comparisons
per corpus, covering every viewport/matrix/codec byte, exceptional coefficient
words, incoming sticky flags, all rounding/flush combinations and invalid
metadata. Rejected calls leave output unchanged.

Original/candidate/candidate/original complete-preparation timing used the
same 200/193/194 cases, 500 repetitions and warm/streaming layouts described
above. Positive values below mean less time; negative values mean a slowdown.

| Corpus | Fused warm reduction | Fused streaming reduction | Cache warm reduction | Cache streaming reduction |
|---|---:|---:|---:|---:|
| Menu | -4.446% | -2.768% | 0.562% | 0.109% |
| Question | -1.275% | -0.861% | -0.976% | 0.148% |
| Reveal | -4.605% | -2.731% | -0.719% | 0.309% |

Fusion was slower, and cache effects were small and inconsistent. Neither
justified live integration. Private source, fixtures and logs are retained in
`codex-fused-capture/` and `codex-clip-parameter-cache/` under the artifact root
below. These studies changed no production source, compiler setting or binary.

## Retained artifacts

Private artifacts under `/home/deck/buzz/performance/optimization/`:

- `codex-skin-recipe/`: source-derived prototype, source hashes, sanitizer
  fixtures, retained initial failure, complete logs and timing summaries.
- `codex-skin-palette/`: independent prototype, poisoned-array/strict-padding
  comparisons and timing results.
- `codex-menu-geometry/`: capture manifest, full corpus and sidecars, inspected
  screenshot, three-corpus shape analyzer/CSVs, both menu comparisons and exact
  reference replay log.

Root/prepared production sources, compiler settings, SDK and installed game
were not changed by these studies. No generated game code, captures or
binaries are committed.
