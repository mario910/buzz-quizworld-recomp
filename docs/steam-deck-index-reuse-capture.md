# Deck index-remap reuse capture, 2026-11-16

Exact index streams recur often in this short heavy-menu capture. A simulated
LRU cache with a 16 MiB payload budget could reuse 89.71% of recorded mappings,
covering 86.94% of their input indices. This supports testing a bounded,
content-verified cache; it is not an implementation or an FPS improvement.

The diagnostic build records logical big-endian index bytes, base, span count,
unique vertex IDs, occurrence mapping and ranked/fallback mode after the original
mapper executes. It changes no output mapping. Capture begins 45 seconds after
the first eligible draw and targets three seconds, but stops at its 128 MiB
byte cap after approximately 1.143 seconds. It records 62,944 draws containing
17,526,795 indices and skips 140 draws above the proposed 4,096-index admission
limit. The largest captured draw contains 3,567 indices. The capture has
62,476 ranked and 468 fallback mappings.

Offline validation checks every occurrence against its recorded index, exact
unique cardinality, first-use or sorted ordering as appropriate, and complete
mapping consistency for identical input bytes. All 62,944 records pass; they
contain 6,478 distinct logical index streams. This also checks that rereading
input bytes for the diagnostic did not produce an observed mapping inconsistency.

The LRU simulation compares complete input bytes, with and without base in the
key. It assumes cached packed vertex IDs and occurrences; base would need to be
applied when materializing vertex references. Budgets include key bytes, packed
outputs and a 64-byte per-entry allowance, but exclude allocator and bucket-table
overhead. These are possible hit rates, not measured lookup/copy performance.

| Payload budget | Mapping hit rate | Input indices covered by hits |
|---|---:|---:|
| 256 KiB | 29.50% | 10.16% |
| 1 MiB | 31.53% | 10.86% |
| 4 MiB | 42.92% | 22.47% |
| 16 MiB | 89.71% | 86.94% |
| 64 MiB | 89.71% | 86.94% |

The table excludes base from the key; including it gives essentially the same
results in this sample. A larger cache does not improve the observed maximum,
but the short capture does not establish sustained working-set size, question
or reveal hit rates, or an appropriate production memory budget.

The 90-second capture run exits cleanly and its post-window screenshot shows
an intact game-selection menu. Capture instrumentation invalidates the old
profile hash for `buzz_index_packed_spans`; the warning is retained. The run is
strictly diagnostic and supplies no FPS evidence. Private renderer source and
archive are restored afterward; the shared workspace, installed executable
and pinned SDK are unchanged.

Artifacts are in
`/home/deck/buzz/performance/optimization/codex-index-reuse/`:
source/generator, diagnostic build, compiler logs, `menu-index.bin`, run logs,
`analyze.py`, `capture-validation.json` and `reuse-analysis.json`.
The 134,222,186-byte capture has SHA-256
`7a0a4001e6b5825e732d18f008d3ba3e6f2a5565d59c1c43b274a4313df3459c`.
The next decision requires a bounds-checked cache prototype, captured-stream
replay and live validation before clean reversed FPS comparisons.

The subsequent [cache implementation and live comparison](steam-deck-index-cache.md)
passes correctness checks but regresses FPS; the simulated reuse does not
translate into a retained optimization.
