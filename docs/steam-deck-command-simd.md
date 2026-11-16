# Deck command-publication SIMD experiment, 2026-11-16

Explicit x86 SIMD makes the isolated host command publisher about one-third
faster, but four clean menu runs show only a small overlapping FPS difference.
The candidate remains private and unpromoted. Stable 30+ FPS is not achieved.

The retained reference's saved user-cycle profile attributes about 7.7–8.3%
of each geometry worker's sampled cycles to `publish_geometry_host_commands`.
Offline annotation contains 660 samples in that function and shows many byte
loads, scalar packing/comparisons and spills around packet traversal. These
are CPU samples, not additive critical-path wall time.

The prototype retains the original packet traversal, physical-tail `memmove`,
semaphore insertion, padding, admission and ordered MachStat/MFC publication.
It uses a 16-byte load and one byte shuffle for a word-aligned big-endian
packet quad, carries temporary state in `__m128i`, and uses packed 32-bit
adds, masks and comparisons. Publication admission already proves record
alignment, packet alignment, owned LS and disjoint state spans. The complete
live r3 value is written before channel operations. The original publisher
remains selectable in the same executable with `BUZZ_HOST_COMMANDS_SIMD=0`;
`1` enables the prototype. The switch defaults off. This private implementation
is x86-only and is not a portable production integration.

## Correctness and scope

ASan/UBSan passes:

- 8,256 direct comparisons with the original **host** publisher: every GPR,
  LS byte, context control and ordered channel/transfer result; includes
  64 wrapped look-aheads, 4,107 split cases, 3,227 multiple-split cases,
  1,088 exact-fit cases, 2,048 null destinations and 20 admission rejections.
- 8,512 complete full-command and 8,320 position-command cases against their
  independent pinned original SPU graphs. These retain the existing host
  contract: r3, link, stack, nonvolatile registers, controls, every LS byte,
  channel arguments and transferred payloads match; other volatile registers
  are intentionally excluded. Existing rejection and admission-proof tests pass.

An initial extra direct comparison against the original retail publication leaf
failed on an empty, zero-record call: the existing host publisher's r3 already
differs there. That leaf invocation is outside the host publisher's supported
single-record full/position-parent contract. The failure log is retained.
It was not reclassified as a passing retail test. The direct host equivalence
suite covers that case, while the complete parent suites independently establish
the supported retail contract. No candidate change was needed to pass them.

Whole-job checks use both switch settings with three repetitions over all
9,696 existing captures. Every ordered output stream and exit state matches
the retained reference, including the established verdicts for old captures
with concurrent inputs or failed capture-relative checks. Each setting executes
28,884 drawn/culled jobs. All 1,500 current-menu captures are exact. This does
not relabel the known historical capture failures as exact.

## Measurements

The standalone timing uses 108 synthetic, nonempty single-record packet layouts
and 21,600 calls per arm, alternating A/B/B/A. State reset and checksum checks
are outside the timer. Channel stubs update the same MFC fields but omit payload
recording during timing, so this measures publisher work, not real DMA cost.
Clang 18 uses O3, AVX2/FMA, strict rounding and `znver2` tuning, without PGO.
Three paired repetitions measure baseline/SIMD ns per call:
315.98/210.86, 301.58/199.58 and 324.93/213.68. Checksums match.

The game uses the retained PGO6 data, with no profile mismatch warning; no new
candidate profile was collected. The original publisher remains 2,042 bytes
of code; the new function is 1,039 bytes plus a 64-byte shuffle table.
Four clean 90-second runs use one frozen executable in off/on/on/off order,
fresh saves and private shader caches. FPS covers 40–70 seconds and process
CPU samples 45–65 seconds. No build, fixture, profiler or other game overlaps.

| Run | SIMD | FPS | Median ms | p95 ms | Min one-second FPS | CPU ms/frame |
|---|---|---:|---:|---:|---:|---:|
| A1 | off | 28.50 | 35.211 | 40.434 | 24 | 151.16 |
| B1 | on | 28.60 | 35.308 | 40.223 | 27 | 150.92 |
| B2 | on | 28.37 | 35.354 | 40.541 | 27 | 151.09 |
| A2 | off | 28.37 | 35.153 | 41.639 | 24 | 152.34 |

Mean FPS is 28.43 off versus 28.48 on (+0.18%); CPU is 151.75 versus
151.00 ms/frame (−0.49%). The enabled runs overlap controls. Better p95 and
one-second minima in this small series do not establish stable 30 FPS.
There is no paired unchanged-reference comparison or round-improvement claim.

All runs progress beyond 88 seconds with every presented-frame delta equal to
one and no searched crash, mismatch, sanitizer or Vulkan error. Effective
settings match: RADV VANGOGH, 1280×720, 100% scale, uncapped mailbox,
512-draw batches, two contexts and zero lane spin. All four second-80 captures
show intact game-selection menus; these post-window images do not certify
exact equality of every measured frame.

## Restoration and evidence

The retained SIMD reference was reproduced by SHA before candidate creation.
The frozen candidate is
`f40999364314f7329b0541d1edea60c80f85eb8bbd1aa9fe2074cd0b40e8ceac`.
The experiment driver restored prepared sources/Ninja and the executable
exactly to `630007cb…` at 07:51 CET. The four live runs finished at 07:58:40.
A separate rebuild at 08:02 changed the prepared executable to `9f244bf8…`
and then restored eight geometry headers. Its executable, object and Ninja log
were preserved under `post-run-rebuild/`. At 08:07 the prepared CMakeLists.txt
also changed. A subsequent restoration attempt failed during regeneration
because that configuration expected the absent `native_image7_simd.cpp`.
The writer was not identified; no shared-source changes were overwritten.
The **current shared prepared build is not claimed restored or qualified**.
Its later changes do not alter the frozen measured executable. The installed
`189814d1…` executable and SDK remain unchanged. Further experiments need
isolated build inputs or coordination with the other writer.

Private source, pinned test generation, initial out-of-contract failure,
sanitizer results, disassembly, binaries/hashes, replay outputs, frame CSVs,
telemetry, captures and scripts remain in
`/home/deck/buzz/performance/optimization/codex-command-relocation/`.
No generated retail source or executable is committed.
