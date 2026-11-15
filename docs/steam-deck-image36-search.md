# Steam Deck image 36 table search, 2026-11-15

The bounded integer-search prototype improved the heavy-menu average from
25.92 FPS for the installed build to 27.05 FPS in a six-run comparison. This
is progress toward stable 30 FPS, not completion: most frame intervals still
exceeded 33.34 ms. The reproducible adapter remains opt-in with
`BUZZ_IMAGE36_SEARCH=1`. Actual-round output checks passed; live round timings
remain limited by differing question and camera phases. The installed build
is unchanged.

## Contract

The pinned EU 1.00 image's 0x15BC/0x15A8 loop searches a private-LS word table.
The adapter finds the first match with bounded AVX2 reads and a scalar tail,
then executes the original final register operations. It preserves every GPR,
LS byte, exit PC and transfer target, including the last unsuccessful read and
per-halfword index increments. It changes no FP arithmetic, DMA, channels,
publication or rendering.

Admission requires owned LS, the audited index mask and count representation,
word alignment and a table wholly inside LS. The complete search must fit the
existing pure-loop budget of 256. Interrupts, nested calls, policy contexts,
step observers, lockstep, stack resets, PM-flow tracing and configured drain
diagnostics retain the original path. The existing runtime helper accounts for
even pairs of steps and both history rings; an odd final read uses the ordinary
hook. Unsupported inputs fall back before mutation.

The generator retains the original block bodies and their PGO identities. It
retargets the initial search edge and entry-table pointer; the original back
edge remains unchanged. Only the checked EU 1.00 source hash gets this binding.
The other supported FP-leaf version retains its original search. Generated
oracles, captures and binaries remain private.

## Validation and menu results

The search prototype passed 42,032 synthetic full-context comparisons under
ASan/UBSan. The bounded version passed 42,042 cases using the actual runtime
history implementation, including unsigned counter wrap, duplicate matches,
table boundaries and runtime admission fallbacks. The tracked fixture adds
six mutation-free rejection cases and fresh-process tests for disabled mode,
PC history, pure-loop disable, LS-0 counting, tail returns and full drain hooks.
Generator tests reject altered entry edges and retain original loop names.

Both prototype settings passed 500 captured whole jobs repeated 20 times:
10,000 executions per setting, not 10,000 distinct jobs. All 500 complete
ordered transfer streams and exit states matched. Repeated replay means were
177.59 us disabled and 99.35 us enabled; these sequential kernel/replay timings
are separate from game FPS. The integrated build had no PGO mismatch warnings.

Six uncapped 90-second runs used fresh private runtimes and cold shader caches.
FPS was measured at 40–70 seconds, process CPU within 45–65 seconds, and a
one-shot capture at 80 seconds. No compilation, tests or profilers ran during
measurement. Effective settings matched: VANGOGH, 1280x720, 100% scale, mailbox,
512-draw batches, two contexts and zero lane spin.

| Arm | Build/mode | FPS | Median ms | p95 ms | CPU ms/frame |
|---|---|---:|---:|---:|---:|
| A1 | installed | 25.63 | 38.324 | 47.939 | 164.94 |
| C1 | candidate off | 25.70 | 38.437 | 46.115 | 165.50 |
| B1 | candidate on | 27.10 | 36.382 | 43.833 | 162.63 |
| B2 | candidate on | 27.00 | 36.573 | 43.254 | 164.07 |
| C2 | candidate off | 25.93 | 38.256 | 45.599 | 165.05 |
| A2 | installed | 26.20 | 37.710 | 44.955 | 163.73 |

Both enabled runs exceeded all controls. Means were 27.05 on, 25.82 off and
25.92 installed. Aggregate CPU differences were small relative to their run
variation; the isolated job reduction must not be presented as a whole-process
CPU reduction. Inspected B2/A2 menu captures showed the expected scene without
an evident rendering defect. Captures after the window do not certify phase
equivalence for every measured frame.

Frozen prototype SHA-256:
`591aa41c7d50bbccfb72f5a36632d3ff037e69128f0236fd3fe5a1d6971a39f8`.
Installed reference SHA-256:
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
The tracked generator rebuild was byte-identical to this frozen prototype
(same SHA-256), and independently repeated the 10,000 job checks per mode.
Its source snapshot, build log and hash manifest are retained separately.

An earlier prototype retained one hook call per skipped step. Its reversed
same-executable series averaged 26.65 FPS on versus 25.92 off. This justified
testing bounded accounting; it is not an additional gain to add to this table.

## Actual-round checks

A separate 335-second diagnostic run enabled the search and periodic readback.
It retained 36 frames and captured 667 image36 jobs, spaced at least 200 ms
apart after 200 seconds. Inspected frames showed active questions, an answer
reveal and score progression with the expected geometry and UI. Because this
run used readback, job capture and diagnostic counters, it is not an FPS result.

All 667 jobs passed with both settings, repeated 20 times (13,340 executions
per mode). All 667 complete ordered outputs and exit controls matched, with
zero concurrent-input or failed-capture verdicts. Sequential replay means were
118.76 us off and 82.92 us on. This adds round I/O coverage to the menu corpus;
it does not establish a round FPS gain or identical live question sequences.

Four fresh 335-second runs used the same frozen executable in off/on/on/off
order, the same scripted input and effective renderer settings, and one capture
at 330 seconds. No compilation, tests or profilers overlapped these runs.
Every measured frame row advanced the presented counter by one. The table
reports fixed elapsed-time windows, not synchronized game-state intervals.

| Arm | Search | Menu 40–70 s FPS | Round 240–300 s FPS | Late 300–320 s FPS | 306–315 s FPS / p95 ms / lowest 1 s FPS |
|---|---|---:|---:|---:|---:|
| A1 | off | 26.13 | 34.92 | 26.75 | 24.67 / 46.771 / 23 |
| B1 | on | 26.83 | 34.95 | 27.50 | 25.33 / 46.359 / 23 |
| B2 | on | 26.87 | 34.95 | 28.05 | 26.78 / 45.662 / 23 |
| A2 | off | 25.70 | 35.35 | 26.35 | 24.44 / 50.043 / 21 |

The menu means again favor enabled mode (26.85 versus 25.92 FPS). The main
round window is effectively unchanged (34.95 versus 35.13). Both enabled late
windows exceed the controls, but their magnitude is uncertain: the 330-second
captures show different questions and camera phases, including one scoreboard
view and other question views. All four have intact geometry and UI. The
306–315-second window was an answer reveal in the separate diagnostic run;
these timings alone do not prove an identical reveal interval in each arm.

Process CPU per frame in the main round window was 96.09, 96.68, 97.52 and
96.71 ms in run order. In the late window it was 141.48, 140.54, 131.58 and
145.14 ms. The phase variation prevents attributing the late CPU difference
entirely to this adapter. Stable 30 FPS is not achieved: menu and late-window
averages remain below 30, and every enabled run has one-second bins below 30.

## Reproduction

Run `python3 tests/image36-generator-test.py`, then on Linux x86 with Clang:

```sh
python3 tests/scripts/test-image36-search.py --workspace /path/to/prepared/workspace
```

Private evidence lives under
`/home/deck/buzz/performance/optimization/codex-image36-search/`,
`codex-image36-search-bulk/` and `codex-image36-search-final/`: source snapshots,
hashes, sanitizer logs, replay outputs, frame CSVs, telemetry, screenshots and
measurement scripts. `gameplay-comparison.json` and `gameplay-profiles.json`
retain the long-run results and effective-settings check. The adapter remains
opt-in while broader optimization and scene-matched qualification continue.
