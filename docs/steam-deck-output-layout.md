# Deck geometry output experiments

Two private experiments target the final geometry vertex stream: exact packed
normal arithmetic and fewer stores for a common dense vertex layout. The
store-layout candidate passed replay validation but did not improve clean
menu FPS, despite its standalone timing gain. Both prototypes remain private
and unintegrated. Stable 30+ FPS remains unverified.

## Exact CMP normal arithmetic

Edge CMP11/11/10 output uses the existing SPU scale/bias FMA followed by a
saturating signed conversion and bit extraction. The prototype clamps each
input magnitude to one using integer bits, performs the original-order native
FMA under RTZ/FTZ/DAZ, multiplies by exactly 2^31 and truncates to signed integer.
The final component codes match; intermediate signed integers outside [-1,1]
are deliberately not claimed equivalent. Clamping prevents SPU exponent-255
values from reaching hardware as IEEE NaNs or infinities.

ASan/UBSan checks compared 1,048,576 arbitrary mixed XYZ triples with the
independent integer SPU FMA/conversion oracle. An optimized exhaustive check
then compared all 4,294,967,296 component bit patterns in each of the X, Y and
Z packing positions with the original vector helper. All matched. The latter
is exhaustive component coverage, not enumeration of all possible XYZ triples.
The component mappings are independent and their final masks are disjoint.
The checks preserve MXCSR control bits and restore the incoming environment.

Complete preparation and emitted index/vertex bytes were compared on three
captured corpora, under both zero and strict publication-padding policies.
Destination guards, unchanged LS/registers and sanitizer checks passed.
Unsupported inputs remain excluded from this standalone fixture:

| Corpus | Files | Admitted | CMP jobs | Strict padding admitted / rejected |
|---|---:|---:|---:|---:|
| Current menu | 1,500 | 1,493 | 887 | 1,259 / 234 |
| Question | 1,500 | 1,349 | 922 | 1,076 / 273 |
| Reveal | 4,000 | 3,879 | 2,697 | 3,150 / 729 |

Exclusions were seven storage-4 menu jobs; 146 additional-blend and five
storage-4 question jobs; and 109 additional-blend and twelve storage-4 reveal
jobs. DMA/guest stubs abort rather than silently provide missing blend data.

Optimized, non-PGO timing includes complete preparation, index emission and
vertex emission, with a reused workspace. Regularly selected admitted cases
include fully culled jobs: 200 menu, 193 question and 194 reveal cases, each
repeated 500 times. Original/candidate/candidate/original ordering was used
for both warm repeated-job and streaming traversal. No live game, build or
profiler overlapped these timings.

| Corpus | Warm time reduction | Streaming time reduction |
|---|---:|---:|
| Menu | 0.195% | -0.518% |
| Question | 0.722% | 0.588% |
| Reveal | 0.459% | 0.144% |

These small changes do not justify game integration. The CMP arithmetic
prototype remains private and unintegrated.

## Dense 20-byte output layout

The second prototype recognizes exactly three fields: float3 at byte zero,
CMP normal at byte twelve, and two Edge half components at byte sixteen.
It snapshots eight vertices, transposes XYZ/normal into four-word records,
packs both texture components into one word, and writes the final destination
directly. It preserves the original normal and half arithmetic. Other layouts
retain the original serializer, and partial final blocks retain the original
field writers and publication padding.

This layout occurs in 534 of 1,229 emitted supported menu jobs, 184 of 874
question jobs and 1,615 of 3,062 reveal jobs. These counts exclude fully culled
jobs and do not imply the same proportion of frame time.

Both layout-only and layout-plus-CMP variants passed the complete captured
byte checks above. A separate ASan/UBSan fixture passed 1,824 synthetic cases
covering arbitrary component bits, all six source-slot permutations, eight
destination alignments, vertex counts 1–33, 63–65, 4,095 and 4,096, and arbitrary
padding words. Seven readiness/capacity mutations per case rejected without
writes, with the same reasons as the original serializer.

The same complete-preparation/output benchmark used original/layout/combined/
combined/layout/original order:

| Corpus | Layout warm reduction | Layout streaming reduction | Combined warm reduction | Combined streaming reduction |
|---|---:|---:|---:|---:|
| Menu | 5.237% | 5.729% | 6.339% | 6.608% |
| Question | 0.801% | 0.140% | 0.845% | 0.807% |
| Reveal | 5.779% | 3.781% | 6.839% | 2.833% |

There is visible timing variation, including between the two combined reveal
streaming measurements. The layout-only variant was selected for private game
qualification because it has the larger demonstrated effect and retains all
original arithmetic.

## Private game qualification

Only the prepared workspace's vertex-output header changed. Geometry-flow PGO
was regenerated using a 90-second menu and a 335-second scripted round; other
translation units retained their original profiles and compiler settings.
Both runs progressed normally, with no logged native failure or mismatch.
The menu and late-round captures were visually intact; they do not certify
every intervening frame. Instrumented training FPS is not performance evidence.
The final candidate executable is
`74e698630a3e2d596333a7309db9c8d78b0bda649a0607b6fe4f1fbc9e75fead`.

Three-repetition whole-job replay compared ordered transfer outputs against
the frozen reference for all 8,196 files across image-38 question, image-39
menu, image-40 question and image-41 question/blend/padding/reveal corpora.
No output differed. Capture-relative verdicts also matched the established
reference counts, including its 348 concurrent-input cases and 102 failures
in older captures; those cases are not newly claimed exact against capture.
All 1,500 fresh menu captures replayed exactly, with no concurrent-input cases
or failures. Together these runs exercised 29,088 whole-job executions.

## Clean release comparison and restoration

Four 90-second runs used reference/candidate/candidate/reference order, with
fresh save/cache copies, X11, 1280×720, 100% scale, uncapped mailbox presentation,
512-draw batches, two draw contexts, zero worker spin and image-36 search.
FPS covers seconds 40–70 and process CPU work 45–65. All effective renderer
profiles matched. Each run progressed past 88 seconds, all timed records had
one presented frame, and all four 80-second screenshots were visually intact.
No build, test or profiler overlapped the clean runs.

| Arm | FPS | P95 frame ms | Minimum one-second FPS | CPU ms/frame |
|---|---:|---:|---:|---:|
| Reference A1 | 28.567 | 41.236 | 24 | 154.991 |
| Candidate B1 | 28.367 | 41.160 | 25 | 154.814 |
| Candidate B2 | 28.167 | 41.236 | 25 | 156.748 |
| Reference A2 | 28.267 | 40.912 | 24 | 156.617 |

Mean FPS was 28.417 for the reference and 28.267 for the candidate. Mean CPU
time was effectively unchanged: 155.804 versus 155.781 ms/frame. Mean shares
of frame intervals above 33.34 ms were 70.69% and 71.05%. Reference variation
exceeds the small mean difference; there is no established useful gain, and
neither arm sustains 30 FPS. The candidate was not promoted.

The prepared header and compiler settings were restored, then rebuilt to the
byte-identical reference executable. SDK status is clean. The installed
executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.

## Private evidence

Artifacts are retained under `/home/deck/buzz/performance/optimization/`:

- `codex-cmp-output/`: arithmetic prototype, independent and exhaustive checks,
  captured fixture, output-layout counts, sanitizer logs and timing summaries.
- `codex-output-layout/`: store prototype, synthetic and captured checks,
  per-arm timings, prepared-header snapshots, fresh geometry profiles,
  qualification logs and ordered outputs, run manifests and screenshots,
  effective-profile comparisons, summary and restoration hashes.

The frozen reference executable is
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.
These experiments do not change the installed executable or SDK.
