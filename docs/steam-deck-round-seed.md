# Fixed engine seed does not reproduce Deck rounds

Two 335-second runs of the same private executable applied the same engine
seed but selected different questions, produced different scores, and reached
different phases at the same elapsed times. This diagnostic is **not a
deterministic round benchmark** and supplies no FPS improvement claim.
Stable 30+ FPS remains unachieved.

## Probe and build verification

In the pinned generated PPU code, `0x24F738` obtains a time-derived value,
constructs a seed and stores it at the session object's `+0x198` field.
`0x24DF18` applies its upper/lower words to the engine states returned by
`0x24C060` and `0x24C0A0`, then forwards the low word to another seed routine.
The two engine getters initialize separate two-word states and their callers
advance those states with rotate/add operations.

The private probe replaces only the generated seed before that store when
`BUZZ_BENCHMARK_SEED` is supplied. It logs generation and application. It does
not freeze clocks, scheduling, animation, or frame progression. Both trials
used `0x12345678edcba987`; both logged its application from caller `0x250488`.
No additional generation or application was logged during either round.
This identifies a live seed path, not every source of question randomness.

An initial attempt edited `port/recompiled/ppu_recomp_013.cpp`, while Ninja
actually compiled `analysis/native-completion/ppu_recomp_013.cpp`. The build
reported no work and the candidate hash equaled the reference. Its run is
excluded. The corrected build preserves the other patches in the compiled
source and verifies that its object rebuilt, the executable contains the probe
strings, and its hash differs. A separate startup race in the waiting driver
failed before launching a game; the archived retry waits for the state file.

## Observed divergence

Both corrected trials used fresh copies of the same saved-data seed and the
same scripted inputs. Effective settings matched: 1280×720, 100% render scale,
uncapped mailbox presentation, 512 small-draw batch size, two draw contexts,
and zero worker spin. They ran sequentially after building and restoration.

The parent harness captured only the game's PID-verified X11 window at these
elapsed times. All ten captures were inspected and rendered intact:

| Seconds | First run | Second run |
| --- | --- | --- |
| 240 | Phil Collins band; correct response | Harper Lee novel; wrong response |
| 270 | Green scoreboard, one correct | Green scoreboard, zero correct |
| 306 | “Show Must Go On” band question | Hogwarts headmaster question |
| 315 | Same band question, timer running | Headmaster answer revealed |
| 330 | Sam Allardyce nickname question entering | Ozzy Osbourne band question active |

The renderer's separate 330-second captures were also inspected and confirm
different questions and scores. Both runs presented beyond 335 seconds, every
recorded presentation delta was one, and the checked logs contained no fatal,
mismatch, assertion, device-loss or Vulkan-error messages. These checks do not
make the workloads equivalent. Screenshot collection is diagnostic, so these
runs are excluded from clean performance comparisons.

An identical initial engine seed is insufficient. Different random-stream
consumption or another random source could explain the divergence; this trial
does not distinguish them. The next useful boundary to trace is question
selection and phase transitions. Neither an elapsed-time label nor a later
scoreboard screenshot certifies the contents of an earlier timing window.

## Evidence and restoration

Private artifacts are in
`/home/deck/buzz/performance/optimization/codex-round-seed-v2/`: source copies,
build driver and logs, executable hashes, functional harness, per-run input
manifests, seed logs, frame CSVs, ten timed window screenshots, two renderer
captures, `audit.json`, `renderer-settings.json`, and `final-verification.json`.
The excluded first attempt is retained under `codex-round-seed/`.

- Reference executable: `d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56`.
- Diagnostic executable: `bd99e1b523f9cf25a3a4668af5c343a4635664eddfa8d767636862556706a493`.
- Compiled source before the probe: `f074a354d6cd2ec009f839d6b027d13a8f6ceeac204c2a8f85a88dfd02d45c74`.
- Compiled source with the probe: `d19c6c254960178be45874db0a85258eb100a57d618abb02e6b028bdc276adb1`.

Prepared sources and Ninja flags were restored; the prepared executable rebuilt
exactly to `630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.
The installed executable remains
`189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
The SDK remains clean. No probe, game assets, generated source, screenshots,
or binaries are promoted to production or committed.

## Follow-up: tracing and reseeding at question selection

A second private diagnostic locates the script callbacks for caching the
current question (`0x138828`), caching the next (`0x138798`), promoting the cache
to current (`0x1384C8`), and shuffling answers (`0x1372C0`). It also observes
question/answer-plan names and speech IDs. Registered callbacks retain the
original implementation and drain its trampoline before recording the result.

For this pinned build, the manager reached through TOC `-0x4470` holds a cache
object at `+0x68`. Its `+0x10` and `+0x14` pointers identify current and cached
questions. Native getters establish the record's file number at `+0xA`, text
number at `+0x22`, and speech ID at `+0x24`. The trace reads these fields and
records monotonic timestamps; it does not infer identity from image similarity.

Both 335-second trials additionally reset the two engine states and the
thread-local seed fields immediately before each cache operation. The latter
writes reproduce the seed routine reached through `0x5B64F8 -> 0x5887A0`.
Selection `n`, starting at one, uses
`0x12345678edcba987 + n * 0x9e3779b97f4a7c15`, modulo 2^64. This is a private
benchmark experiment, not a production gameplay change.

All five reseeds matched across runs. All four engine state words also matched
after every corresponding selection. Nevertheless, three selected identities
differed:

| Selection | First run: file/text/speech | Second run: file/text/speech |
| --- | --- | --- |
| 1 | 12 / 774 / 137348 | 10 / 1249 / 138465 |
| 2 | 11 / 3097 / 137427 | 11 / 3097 / 137427 |
| 3 | 10 / 1381 / 139271 | 12 / 624 / 138969 |
| 4 | 12 / 534 / 138864 | 10 / 2124 / 139197 |
| 5 | 11 / 3091 / 137476 | 11 / 3091 / 137476 |

This pattern is consistent with different preexisting file-selection ordering,
but does not prove the cause or exclude another source of variation. Changing
the seed at the cache boundary is still insufficient. The fifth question
became current 112.550 versus 115.609 seconds after the first, a 3.059-second
relative timing difference.

At 306 seconds the screenshots show a musical-composer question in one run
and a basketball-team question in the other. At 330 seconds both X11 captures
show the same song-completion question and a score of one; the renderer's own
330-second captures both show the green scoreboard with that score. Thus even
matching final question/score observations do not establish matching content
inside an earlier measurement window.

All ten timed X11 captures and both renderer captures were inspected and
rendered intact. The runs presented through 334.717 and 334.709 seconds,
respectively, with presentation deltas of one and no checked fatal, mismatch,
assertion, device-loss or Vulkan-error messages. Effective renderer settings
match the preceding study. No FPS comparison is claimed for these instrumented
runs, and the reseed probe is not retained in production.

Artifacts are in
`/home/deck/buzz/performance/optimization/codex-question-selection/`, including
the probe source, frozen executable, build/restoration logs, captures,
`analyze.py`, `question-events.json`, `audit.json`, `renderer-settings.json`,
and `final-verification.json`. Diagnostic executable SHA-256:
`80c5663c17dcf38e5829498e1e6526664a2015da771a4456afcedad350ac483a`.
Both patched source files and Ninja flags were restored, the prepared binary
again matches `630007cb…`, and the installed binary and clean SDK are unchanged.
