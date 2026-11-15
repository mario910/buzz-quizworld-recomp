# Steam Deck packed vertex-input experiment

The live Vulkan integration passed CPU, GPU and gameplay checks, but is not
promoted. Two reversed menu series showed small mean leads (0.7% and 0.8%)
over the retained reference, with overlapping runs and inconsistent frame-time
tails. The final enabled build still spent 66.6% of measured intervals above
33.34 ms. Stable 30+ FPS remains unachieved; a useful repeatable improvement
across the target scenes has not been established.

## Implementation and contract

This follows the exact decoder feasibility work in
[the dependency investigation](steam-deck-critical-work.md). A port-owned
adapter captures FLOAT3 positions, CMP1 normals and optional SF2 UVs into
immutable uploads, reducing eligible vertex strides from 32/48 bytes to 16/20.
Vulkan uses native float3 position input and integer normal/UV input. Integer
shader helpers reconstruct the original CPU decoder's binary32 values,
including half subnormal flushing and NaN payloads. Position W remains one.

Admission requires a valid guest shader pair, the actual Vulkan backend,
prepared stable source bounds, frequency at most one, and exactly the supported
attribute types. All vertex references must fit every source range. Immediate,
inline, unsupported and other-backend inputs retain the original path. Source
bytes are copied before submission; the GPU never reads guest memory directly.

The original attribute mask remains in the low 16 bits. High bits identify the
packed layout and attribute roles; the engine hashes the full mask and stride.
The shader rewrite requires exact unique declaration/assignment anchors and
fails explicitly if they differ. Attribute IDs are mapped through layout slots,
not treated as Vulkan locations. Diagnostic pass dumps disable packing.
Build and runtime switches are opt-in.

The first backend stored two 16-entry metadata arrays per pipeline. The second
version stores only a flag and position/normal slot numbers in one word. It also
rejects an undersized shader-rewrite buffer before capacity arithmetic. Both
source versions are archived privately.

## Validation

- CPU admission, bounds, ordering, wrapping, fallback and shader-rewrite tests:
  28,834 cases passed under AddressSanitizer/UndefinedBehaviorSanitizer. The
  final revision also checks zero/insufficient rewrite capacity.
- Actual Vulkan vertex-input readbacks: 576 packed/reference framebuffer pairs
  each with batch modes 0, 3 and 512, totaling 1,728 pairs per implementation.
  Both implementations passed. Tests cover attribute-role permutations,
  two/three-attribute layouts, CMP/SF2 edge bits, float3 default W, and synchronous
  and asynchronous drawing. Independent expected pixels prevent a pair of blank
  frames from passing. These complement the earlier 8,388,608-word compute proof.
- A 90-second menu audit checked at least 237,712,004 packed vertices without a
  mismatch, covering about 63% of all fetched vertices. Its capture was intact.
- A completed 335-second scripted round audit checked at least 660,389,092
  packed vertices without a mismatch; the final capture shows an intact question
  scene. It does not certify the scoreboard or every frame's camera phase.
- An earlier queued round launcher ended with SIGTERM at about 309 seconds and
  lacked a final capture. Its partial results are excluded from completed-run
  claims. The direct rerun above completed normally.

The live audits compare captured packed bytes, reconstructed through the SDK
CPU decoder, against the original fetch for each admitted vertex. They do not
compare every live GPU output pixel. Synthetic GPU parity supplies that separate
check. Native SPU job code was unchanged; no new SPU replay claim is made.

## Clean game measurements

Each sequential run used a fresh save/cache, the same menu inputs, X11,
1280x720, 100% scale, uncapped mailbox presentation, 512-draw batches, two
contexts, zero worker spin and image-36 search enabled. Timing covers seconds
40–70 of a 90-second run; capture is at 80. Effective renderer settings and
executable hashes were checked. All twelve menu captures were inspected.
No audits, profilers, builds or benchmarks overlapped these timing runs.
Script-relative windows and post-window captures do not establish exact scene
identity for every measured frame.

A is the frozen qualified reference; C is the candidate with packing disabled;
B is the same candidate with packing enabled. Both series used A1/C1/B1/B2/C2/A2.

The first series averaged A 28.750, C 28.567 and B 28.950 FPS. Changed renderer
functions discarded their old PGO counts, so that result alone was insufficient.
After reducing metadata, only the draw-engine and Vulkan-backend translation
units were retrained using menu-on, round-on and menu-off workloads. Other units
retained PGO6. The final build reported no stale-profile/hash mismatch warnings.

| Arm | FPS | Median ms | p95 ms | Aggregate CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.733 | 35.073 | 40.066 | 154.779 |
| C1 | 28.333 | 35.207 | 41.185 | 155.691 |
| B1 | 28.933 | 34.736 | 40.011 | 153.818 |
| B2 | 28.600 | 34.966 | 41.399 | 153.607 |
| C2 | 28.667 | 34.979 | 39.756 | 155.441 |
| A2 | 28.333 | 35.430 | 41.169 | 155.258 |

The final means were A 28.533, C 28.500 and B 28.767 FPS. B's mean CPU work was
153.712 ms/frame versus A's 155.019, but aggregate parallel CPU work is not
elapsed frame time. B's p95 ranged from 40.011 to 41.399 ms and its lowest
one-second bin was 25 FPS. No clean question/reveal FPS qualification followed;
these results do not justify a stable-30 claim or promoting this implementation.

A separate CPU-only ABBA benchmark includes packed admission plus copying and
compares the existing vertex stream path. For 256 scattered three-attribute
vertices it measured about 3.63 versus 2.76 ns/vertex. This confirms a local
saving, not its contribution to frame time; shader execution and synchronization
are absent from that microbenchmark.

## Evidence and disposition

Private artifacts are under
`/home/deck/buzz/performance/optimization/codex-packed-vertices/`:

- `source/` and `compact-source/`, with source hash manifests, retain both
  implementations, adapters and CPU/GPU fixtures. To reproduce integration,
  materialize the chosen source snapshot in an isolated checkout/workspace of
  the parent revision with the recorded SDK pin; the build scripts use that
  workspace's generated backend. These fixtures are not standalone SDK copies.
- CPU/GPU pass logs, live-check logs/captures, both six-run comparison scripts,
  frame CSVs, settings checks, manifests and telemetry remain available.
- `raw-profiles/`, `renderer.profdata`, training/build logs and `pgo-scope.py`
  preserve the two-unit profile refresh. `bench.c`, `bench.py` and `bench.log`
  preserve the separate local benchmark.

Executable SHA-256 values:

| Build | SHA-256 |
| --- | --- |
| Qualified reference | `630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d` |
| Initial packed candidate | `12e8132d385bfa7f6357ff0cf9f336dec78b2946304f3f24243a69c58c10eea7` |
| Compact metadata, old profile (not FPS qualified) | `12676ac44c76f2e0461a20bcbf0e8a34c1de016924f68eb039dcdb6562a055da` |
| Compact metadata, refreshed profile | `f82cea255a8b309f3023dfb3ad1b5fa3be91147ae924bba3ff9c8e83572514f0` |

The prototype is archived rather than added to production. No generated game
code, assets, captures, binaries or private training profiles are committed.

Production source and the private build configuration were restored. The
rebuilt workspace executable matches the qualified reference byte-for-byte.
SDK pin `79efc280dad27271d5f68077acb0247df1e88dc7` remains clean. The installed
executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
Restoration logs and `restoration.json` retain these checks.
