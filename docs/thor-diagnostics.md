# AYN Thor investigation (2026-11-08)

Status: **the reported sustained slowdown/overheating is not fixed or
qualified**. Do not infer gameplay performance from loading-screen FPS.

Device: Android 13, QCS8550/Adreno 740, system Vulkan driver. EU 1.00,
reference29 build properties. No device security, governor, thermal threshold
or driver changes were made. Tests used an application deadline and a separate
device-local supervisor, stopping at 63 C on any watched CPU/GPU sensor.
This is a conservative diagnostic threshold, not a manufacturer safety rating.

## Evidence

- The original unrestricted run failed from the user's perspective: about
  1 FPS, full CPU, temperatures above 70 C, requiring a device reset. Recovered
  logs are partial/zero-filled; they do not identify a conclusive crash cause.
- A 30-second diagnostic restricted to CPUs 0–2 (the three little cores on this
  device) progressed to language selection and buzzer instructions at about
  30 FPS. This is not unrestricted or gameplay qualification.
- Instruction-pointer sampling attributed the busiest native thread to the
  original SPU audio/MultiStream routines, including the compressor. That
  thread used approximately 59–60% of one little core. The render thread used
  approximately 26–28% of one core during these startup scenes, including shader
  compilation. This is sampled evidence, not proof of the original stall's
  root cause. Do not replace exact SPU arithmetic globally on this evidence.
- Before the audio change, the measured audio period rate was already
  186.6–188.9 blocks/s (nominal 187.5). The hypothesis that this device was
  continuously mixing at 500 blocks/s was **not confirmed**. Observed queue
  snapshots were 1280–2816 samples.
- After the audio change, observed queue snapshots were 512–1024 samples and
  the measured period rate remained near 187.5 blocks/s. These short, thermally
  interrupted runs do not establish a repeatable FPS or CPU gain.
- A fresh-settings run logged `configured=0 ... cap=30` and displayed about
  30 FPS at language selection. Near the end, a CPU sensor jumped from 54.3 C
  to 77.2 C between one-second samples; the supervisor killed the game. Another
  candidate run stopped at 63.7 C. Both are failed thermal tests, not passes.
- A post-test system snapshot, with the game stopped, also showed activity in
  the dual-screen launcher/assistant and compositor. Do not attribute every
  SoC temperature change solely to the game, or stop unrelated apps silently.

## Changes retained for review

1. Android defaults to a 30 FPS presentation cap without a settings file.
   Explicit saved settings (including uncapped) still win. Desktop behavior is
   unchanged. This removes unintended unlimited startup work, not the heavy
   game's computation.
2. POSIX cellAudio checks SDL queue room before mixing and issuing guest
   period events. A missing device uses an exact rational monotonic clock,
   with no missed-block catch-up burst. Windows is unchanged. The fallback
   clock's cadence, fractional phase, jitter, stall rebase and reset are tested.
3. Debug-only, opt-in bounded runs, affinity and native PC sampling are
   documented in the Android guide. Normal launches have no diagnostic timer,
   signal handler, sampling file or affinity restriction.

Build and checks: Android Debug APK succeeds; the fallback-clock test passes
with ASan/UBSan; the PC preferences/menu regression test passes. The installed
initial safety/pacing candidate APK SHA-256 was
`fc12a31a477fa730d7155bc2c5990d0467a95867e15a4d7ce4890091174a6800`.
Device logs, screenshots, APKs and matching unstripped ELFs are private test
artifacts, not distribution content.

## Subsequent repairs and validation

- Android now enables the SDK's existing, version-qualified SPIR-V disk cache
  before rendering starts. Previously only `RSX_SHADER_CACHE_DIR` was set,
  which is not the Vulkan translator's persistent cache. A cold run translated
  shaders and left complete atomic cache files even when stopped by the guard.
  The next run recorded 19 disk hits and 3 new translations; a later run
  recorded **22 disk hits and zero translations** in its initial window.
  Driver pipeline creation is not cached by this change.
- Added opt-in, hash-bound adapters for nine SPU audio leaves: compressor
  `2FF18/301D8`, mixer `B078/C758/C8D4`, transform loops
  `4E00/4F24/513C/51F8`. Other functions, mixed/double-precision code, DMA,
  channels, reciprocal estimates and interpolation are unchanged. Registers
  are local only for owned LS with debug hooks disabled. The ARM64 path scopes
  the existing checked RZ+FZ helpers and restores FPCR/FPSR at each exit.
- Local-register allocation alone did not produce a meaningful Thor speedup.
  With checked NEON arithmetic, representative, reset-input microbenchmarks
  showed typical times around 34 -> 15 us for compressor `2FF18`,
  11 -> 4.5 us for `4E00`, and 6 -> 2.5 us for `513C`. ABBA samples include
  scheduling outliers; these numbers are **not full-game FPS gains**.
- The microbenchmark resets both registers and LS outside each timed call.
  Reusing in-place filter outputs causes progressively overflowing inputs and
  is not a valid like-for-like comparison. Synthetic finite LS values are
  encoded big-endian. The test also covers arbitrary words, signed zeros,
  denormals, extended SPU exponents, saturation boundaries, overlapping buffers,
  external LS, link returns and both conditional tail exits.
- **7200 complete-state comparisons passed on the actual Thor**, including
  exact FPCR/FPSR restoration. The host ASan/UBSan comparison also passed.
  Bounded in-game compare mode then passed **144/144** complete-state checks
  across all nine entries, retaining original results. None of this validates
  every possible future game state or sustained gameplay performance.
- The optional comparison shadow is lazily allocated, not embedded as a large
  per-thread TLS object. Normal workers do not pay for a full diagnostic SPU
  context. Original/native/compare wrapper routing has an ASan/UBSan test.
- Per-thread status/residency confirmed productive PPU, SPU, renderer and job
  workers stayed on CPUs 0–2 in restricted diagnostics. Existing ART, binder
  and Android UI threads retained their ordinary masks; no global affinity
  policy was installed.
- A native two-leaf run without screenshots completed its bounded interval
  with a maximum observed sensor value of 62.5 C. Later screenshot-free runs
  still reached the 63 C stop threshold. Screenshots can perturb measurement,
  but have **not** been established as the cause of all thermal excursions.
- The final nine-leaf unrestricted-core smoke test, with a warm shader cache,
  was stopped at **64.1 C within about three seconds**. It did not reproduce
  or diagnose the original sustained 1 FPS failure. No longer unrestricted
  stress run followed. Android's thermal service reported `HAL Ready: false`;
  its empty temperature list/zero status must not be treated as proof of
  adequate cooling.

The audio adapters remain OFF in ordinary CMake/reference29 builds. The test
APK explicitly selects them. Its final SHA-256 is
`5c062324ae6412fd39e02a81f41ad42320697787bf97d47cb1c5a309f149156a`.
The large comparison shadow's lazy allocation reduced ELF TLS reservation by
268,848 bytes; this is a layout observation, not a measured RSS/FPS gain.
Generation, source cache values, correctness
tests and rollback are documented in the Android guide. Generated game-derived
sources, runtime cache, captured states and binaries stay private. The newly
installed APK is recorded with the private test artifacts; the older hash
above identifies only the initial pacing candidate.

## Follow-up: Standard mode, automatic fan

The owner confirmed Standard power mode and automatic fan control. No global
governor, affinity, fan policy, security setting or thermal limit was changed.
The diagnostic CPU mask below remains per-process and opt-in.

- The POSIX coherence-line lock now uses test-and-test-and-set with relaxed
  polling and an architecture pause hint, rather than repeatedly issuing an
  exclusive exchange while held. The Windows implementation is unchanged.
  Host ASan/UBSan and ThreadSanitizer contention tests passed, as did the SDK
  registry regression suite and 195 coherence checks on Thor. A noisy isolated
  lock comparison does not establish a game FPS improvement.
- The Android link version script reduced defined public symbols to the
  audited SDL/JNI surface. Dynamic relocations fell from 59,487 to 1,655 in
  this build. This is a binary-layout observation, not an FPS claim.
- Added independent visibility/menu pause ownership. A bounded HOME/resume
  test retained the same PID and kept an already-open menu paused. Closing
  the menu then resumed the guest. Initial surface readiness does not emit
  spurious system-menu callbacks; existing surface-lifetime locking remains.
- Some earlier captures were invalid visible-game tests: the device was
  asleep, and default `screencap` selected the secondary display. Subsequent
  tests explicitly woke the device and captured its primary physical display.
  Neither a black sleeping display nor a paused redraw counter qualifies FPS.
- Replaced high-overhead per-thread polling with a lightweight monitor for
  UI runs. Thermal excursions still occurred in earlier lightweight runs;
  monitoring overhead is not established as the sole cause.

The lifecycle candidate APK is identified by SHA-256
`15ccbaa4d49f0634f2240aa5f6267c4073cb0270b1be4d0a64f9460367e11006`.
Its matching unstripped ELF SHA-256 is
`d5e7cd23f51da1b0d2634edaf7908307e87fc13cba3bca312f3ea5c087d094e5`.
Private diagnostic series, all with native workers restricted to CPUs 0–2:

| Series | Result | Maximum sampled CPU/GPU sensor |
| --- | --- | --- |
| probe20 | HOME/resume with menu still open; visible language/settings UI | 53.3 C |
| probe23 | Observed language/buzzer UI progression with menu pauses | 52.1 C |
| probe24 | Failed: presentation stopped at boot, SPURS event wait blocked | 52.9 C |
| probe25 | Original audio; boot kept presenting through bounded interval | 52.5 C |
| probe26 | Native audio; boot also kept presenting through bounded interval | 53.3 C |
| probe27 | Native audio; visible original intro, mostly near 30 presentations/s | 54.5 C |

The probe24 stall left a productive SPU thread while a PPU event-flag wait at
`0x701EB380` blocked. It is a concrete failure, not a thermal abort. Neither
the original-audio comparison nor the subsequent native-audio run reproduced
it, so these observations do not isolate its cause. The monitoring helper also
had a progress bug: repeatedly visiting both tail rows reset its timeout even
when the newest row was unchanged. It now compares the maximum frame in each
snapshot once against the previous snapshot, also allowing a newly opened CSV
to reset a previous run's serial; independent thermal/deadline stops were
active throughout. No result here establishes sustained studio/gameplay FPS.

### First studio-frame investigation

The next APK exposed the desktop's startup-only video skip through a bounded,
debug-only Android extra; its ELF export audit passed with 11 public JNI/SDL
symbols. `probe28` (native audio) and `probe29` (original audio plus sampling)
both stopped making visible frame progress at the first menu/studio transition.
The 8-second progress watchdog ended those tests. Their maximum sampled
CPU/GPU sensors were 54.9 C and 59.3 C respectively. Thus the native audio
adapters are not required to reproduce this transition problem.

The final 1000 CPU samples from probe29 included SPIRV-Tools' debug def-use
reconstruction/comparison on the renderer, and the Adreno shader compiler.
This is evidence of compilation work, not proof that the blocked SPURS wait
is the root cause or that a true deadlock occurred. The sampler's synchronous
cache-stat query can itself wait behind a translation; tail samples are not a
precisely timestamped wall-clock window.

Android now defaults `BUZZ_ANDROID_SHADER_CONTEXT_CHECKS=OFF`, selecting the
upstream `SPIRV_CHECK_CONTEXT` build option before configuring shader libraries.
This removes expensive developer IR checks, not shader legalization,
optimization or the Vulkan binding contract. It is explicitly reversible and
does not affect desktop builds. Generated build commands confirmed the check
define absent; the APK build and complete 11-symbol export audit passed.

**This change did not fix studio performance.** Probe30, with checks disabled,
continued constructing surfaces but had no new visible studio frame within its
bounded interval (maximum 56.9 C). Probe31 reused that APK and its warm cache,
with zero new translations and 81 disk hits at an intermediate snapshot, then
encountered further compilation/rendering work. Its CSV recorded a 24,416.8 ms
FIFO-heavy gap before the first subsequent presentation, followed by 586.2 ms
and 803.5 ms intervals. These are first-visit transition measurements on three
little cores with profiling, **not steady-state studio FPS**. Maximum sensor:
58.9 C. Both runs retained the 63 C thermal guard and 60-second app deadline;
the short no-frame watchdog was intentionally disabled to allow compilation.

Installed shader-context-off APK SHA-256:
`a5a9e2bb2b7a8aea07a01232b88f65736a7c29ff6a3f4708a6e47afd39e84d3b`.
Matching unstripped ELF SHA-256:
`2d18e2d7d1392151410fffb4531dc164702e020dea46b7bf92e48ea7e05807a1`.

Finally, **probe32**, without native-worker affinity or PC sampling, failed
the thermal guard almost immediately: its first two device-supervisor reads
were 57.7 C and 66.1 C, 250 ms apart. The supervisor killed the app; the host
observed exit about 3.6 seconds after launch. The preflight was below 52 C.
This is a failed unrestricted test, not a crash or a safe sustained-performance
result. No further unrestricted stress run followed, and the app was left
stopped. Standard/Auto does not by itself qualify this build for sustained use.

### Owner-authorized 80 C diagnostic threshold

The owner subsequently explicitly requested an 80 C stop threshold with the
fan active. The private supervisor now accepts a validated, explicit threshold
(default remains 63 C; maximum 80 C). The application deadline and device-local
supervisor remain enabled; no hardware thermal policy was modified.

Probe33 used all available CPUs, the installed shader-context-off APK, system
Adreno 740 driver, warm translation cache and startup-only video skip. It began
presenting studio frames, at roughly 7–8 presentations/s. Repeated one-second
backend intervals showed about 750 ms waiting on submission/context fences
and 65 ms of backend draw CPU time per eight frames; these CPU-observed waits
are not GPU timestamp measurements. The sensor crossed the requested threshold
at 80.8 C between quarter-second samples; the supervisor killed the app. This
was a failed thermal qualification, although it established post-startup frame
progress on unrestricted CPUs.

Probe34 excluded only CPU 7 (native-worker mask 0x7f) as a diagnostic. It
continued at 30 presentations/s through simple startup screens, but around
the first studio transition ADB shell calls stopped responding. The sample
at 35.3 seconds reported frame 845; later probes timed out. Neither the final
process state nor the device supervisor's final log could be retrieved, so
the process deadline must not be reported as a successful shutdown. A scoped
ADB transport reconnect was attempted; the device then disappeared from ADB.
Further device testing requires the owner's recovery/reconnection. A kernel/
driver-level hang is a hypothesis, not yet a proven cause; an in-app watchdog
cannot guarantee recovery from an unresponsive OS/driver.

For the next isolated comparison, a regular a6xx/a7xx Mesa Turnip R8 ADPKG
was downloaded from its publisher's GitHub release, not an a8xx experimental
package. Archive SHA-256 matches GitHub's asset digest:
`e634db0f929e2205e95511c769071817d0390180ec72c8e690bc76375e813715`.
It was subsequently imported through the existing application-private importer
in probe36; the system driver was not replaced. Downloaded drivers and device
test artifacts remain private. `GPU -> Options -> Use system driver` rolls back
the selection after restarting the process.

### Recovery and Turnip comparison

The owner confirmed the device UI still responded during the earlier ADB loss.
ADB subsequently recovered without a device reboot initiated by this agent.
No game PID remained. The recovered probe34 supervisor log ends with `Target
already exited`; this confirms later process exit but does not establish its
precise time or prove a kernel/driver hang. The earlier whole-device-hang
hypothesis is not supported by the owner's observation.

The private supervisor was also corrected to compare `/proc/uptime` against an
absolute deadline. Counting nominal 250 ms sleeps can extend a deadline under
load; the independent Java deadline already used a monotonic absolute clock.
Actual polling can overshoot the temperature threshold between samples; an
80 C request is not a guarantee that no sensor ever exceeds exactly 80 C.

Probe37 confirmed `Driver loaded from ZIP package` / `Turnip Adreno (TM) 740`.
Observed presentation-call time fell to roughly 4–6 ms versus about 90 ms on the
system driver in probe33. Conditions were not a matched ABBA GPU benchmark;
this is a promising observation, not a universal driver-speed claim. Seven
native-worker CPUs still reached 80.4 C and triggered the guard. The studio
menu was visibly rendered, rather than just inferred from frame counters.

Probe39 used CPUs 0–4 and a 30 FPS limit with Turnip and completed its bounded
run (maximum sampled CPU/GPU sensor 75.6 C). The 40–58 second main-menu window
averaged **5.44 presentations/s**. It is a stable diagnostic scene, not a
qualified full-device/steady-gameplay result. A separate 60-FPS-limit probe38
encountered a save-loading error (no save file exists; no save was removed),
so it is not a valid studio comparison.

An opt-in candidate allowed the existing FIFO-readiness proof at a 30 FPS cap.
Its pure policy ASan/UBSan test and Android build/export audit passed, but
probe41 remained around 5–6 FPS. The change was removed from source rather
than promoted. That run visibly rendered all six monitor logos and the menu
panels; BUZZ then successfully opened the game-mode menu. Probe42 retained
the immutable candidate for profiling: audio, admitted host geometry, image36
job work, DMA and coherence locking dominated the final CPU samples. Only
about 0.4% of the final 1400 samples lay in the custom driver. These CPU sample
fractions do not measure the frame's dependency-critical path.

Private candidate identity for probe41/42: APK SHA-256
`dc4cf8ddcbcda2329326cf256b2a58a9bec82ccc87efb580fe21a2ecf303248b`,
matching ELF SHA-256
`4ea7ed3290f8c7719eeab7202f8920cdaf96cf32fea363252db5bbbb6fdb26ba`.
No usable-FPS claim is made from successful input/menu navigation alone.

The next debug APK exposes a bounded `buzz_test_idle_ms` override through
SDL's existing `nativeSetenv`. Normal launches retain the 4 ms FIFO retry.
Probe43 selected 1 ms, retaining the 30 FPS cap, Turnip and CPUs 0–4. It rendered
the visible studio menu at **5.53 presentations/s in the 38–50 second window**,
with a maximum sensor of 75.6 C. More, shorter waits did not remove the total
producer-dependent idle time. It is not an established improvement over
probe39's approximately 5.44 FPS (which used a different time window). No
default retry-time change was made. The corrected device-local hard deadline
ended probe43 at about 53.8 seconds; the process was confirmed stopped.

Installed idle-diagnostic APK SHA-256:
`122f51afcc3087fabc031126572dd306829e04b9809d9d6fea6511a936fbc231`.
The failed ready-at-30 policy experiment has been removed; the selected custom
driver remains application-private. Subsequent work should address productive
audio/geometry and job handoff costs, not assume shorter polling, a build flag
or input responsiveness demonstrates usable FPS.

## ARM64 animation and ATRAC follow-up

The full image40 native animation owner was not part of the Android build.
It now accepts the pinned adapter on ARM64, uses the existing paired NEON
vectors and exact exceptional-lane repair, and saves/restores FPCR **and**
FPSR. Desktop AVX2 qualification is unchanged. Android remains explicitly
opt-in (`PORT_ANIMATION_JOB_DEFAULT`), not a new reference29 default.
The missing masked 8-lane load/store operations preserve sign-mask semantics
and never access disabled tails. The native Thor test passed all 256 masks
and 0–8-lane tails at a protected-page boundary.

Probe44 checked at least **40,000 complete animation jobs** against retained
job-boundary I/O with no reported mismatch. The checker is not an FPS test.
Probe45 used native-only animation on CPUs 0–4: 5.71 FPS in seconds 38–47,
median interval 170.3 ms, maximum sensor 75.2 C. This does not establish a
meaningful gain over the previous approximately 5.5 FPS diagnosis.

The existing bounded-skin/coarse-clip geometry candidates were also tested,
without relaxed reciprocal or changed output padding. Probe47 remained at
5.68 FPS in the same 38–47 window (75.2 C maximum). Probe50's whole-job checker
passed at least **55,000 geometry jobs**; three observed concurrently-changing
inputs were explicitly unrecordable, not successful comparisons. No reported
output mismatch. These optional candidates have no demonstrated phone FPS win
and their source defaults remain OFF.

Probe51 changed both scheduling constraints: CPUs 3,4,6 and two list workers.
It reached 7.92 FPS in seconds 38–47 (median 123.7 ms, maximum 77.2 C).
Because two variables changed, this is a configuration observation, not a
40% animation/kernel-speed claim. Probe52 with four workers and CPU sampling
remained around 7–8 FPS. CPU frequencies on the selected big cores were
observed at 2.8032 GHz. Its final 2,000 CPU samples still included coherence
locking, host geometry, ATRAC and image36 work; the custom driver accounted
for only 0.6%. CPU samples cannot establish the frame's critical wait path.

Two additional hash-pinned ATRAC filter blocks (`36538`, `36710`) now have
optional register-localized, scoped NEON variants. The whole decoder and
channel behavior are not replaced. `tests/audio-atrac-test.c` passed 1,600
complete-state cases under host ASan/UBSan and natively on Thor, including
aliased streams, foreign-LS fallback, finite loop trip counts, all three tail
continuations and FPCR/FPSR restoration. The initial test fixture lacked the
36538 finite-loop bound and its alarm correctly terminated it; that fixture
was corrected before the gate passed. Probe53 then passed all 176 live
comparisons (16 each for the previous nine DSP blocks and these two).
Native-only probe54 reached 7.32 FPS in seconds 38–47, maximum 79.6 C.
There is **no established whole-game FPS improvement** from the ATRAC change.

Unrestricted probe49 was stopped by the existing thermal guard at 81.6 C
about seven seconds into startup. The 0.25-second sampling interval cannot
guarantee zero overshoot above 80 C. No higher threshold or global power/fan
change was made. This is not a qualified unrestricted configuration.

Several affinity runs are **invalid startup tests**, not FPS measurements:
46/48 were rejected before game execution; 55/56/57 also failed to apply the
requested exact CPU set. Probe57 reported requested `0x58`, observed `0x18`.
The test must not silently claim three-core results when the kernel admits
only two. Affinity is now requested on the foreground SDL thread, with bounded
surface/focus waiting and startup task-profile retries; UI and Java watchdog
affinities stay unchanged. A planned pre-Vulkan diagnostic abort removes only
the app-private driver-pending marker so it does not quarantine a driver that
never ran. The earlier invalid launch had caused the normal recovery policy
to deselect Turnip; the same already-imported driver was restored, with no
system-driver change. Prior successful device identity is recorded in each
run; stale logs are not evidence of the failed launch's driver.

The runner now saves/removes only its previous frame CSV before a new run,
so rejected launches cannot masquerade as fresh frame progress. It also
captures PID-filtered Android lifecycle logs. The bounded debug Activity can
select 1–4 list workers and capture a capped 10-second dependency timeline;
these are diagnostics, not ordinary-launch scheduling defaults.

Latest private diagnostic APK SHA-256:
`8e16f3e20a7d197a37354b84074872eb98ac5dc366b3a5f17ca09c98fb4b8dea`;
matching unstripped ELF:
`2332d42a1ba21dec02825c6d6bb297699eaede4972b0384dd3f8c8919d1e02aa`.
No game sources, firmware, traces or driver ZIPs are published.

### Remaining target

Probe58 finally captured the dependency timeline on CPUs 3–4 with two list
workers, Turnip explicitly confirmed in the **new run's** log. The 32–42
second studio window contains 290,191 spans, **zero dropped**, and 48 complete
guest-flip intervals (median 199.9 ms). That flip rate is not a substitute for
host-present timing. Main PPU thread 1 spent about 180.6 ms/frame blocked in
`event_flag_wait` at guest LR `0024F27C`. Chain `701EC500` parallel-list spans
cover about 173.6 ms/frame, averaging 138.3 jobs/list. Image41 contributes
about 3,057 job calls/frame, 228.8 ms/frame **summed job elapsed time**, which
overlaps across workers and cannot be added to the wait/chain figures. The
RSX FIFO drain spans total about 36.2 ms/frame; this is not GPU execution time.

This supports geometry-list completion as the next dependency-critical target,
not the hypothesis that audio/kernel sampling percentages alone explain the
frame. Do not optimize the eight long guard spans as if they were serial frame
stalls: these include independently idle chains. The next substantial approach
is safe reuse/batching of repeated static geometry work, with explicit input
content/camera/skin/format admission and whole-job publication comparisons.
Do not cache by guest address alone, skip dynamic meshes, drop jobs, weaken
coherence or substitute approximate projection to inflate FPS.

The trace and matching binary remain private under
`D:/buzz-android-tests-20261108/probe58-two-big-timeline`; use
`tools/inspect/inspect-chain-timeline.py` and SDK `tools/ftrace_report.py`.
The installed APK is the matching timeline-fixed diagnostic above. The game
is stopped after tests. No playable-FPS, sustained-session or unrestricted
device qualification has been achieved.

Keep tests bounded and do not exceed the owner's explicit 80 C diagnostic
threshold (63 C remains the default). Confirm the
device is awake and the primary game display is selected. Power/fan settings
are now confirmed; qualify a fixed, repeatable studio/gameplay scene and measure complete
frame/CPU effects of the scoped audio adapters. Preserve the exact ELF per run
and compare matching conditions;
little-core diagnostics cannot establish full-device performance or a fix for
the original stall. Do not disable audio, alter SPU rounding globally, lower
resolution or remove the thermal guard to manufacture a speed result.
