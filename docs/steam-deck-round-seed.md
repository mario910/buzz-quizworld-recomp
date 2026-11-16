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
