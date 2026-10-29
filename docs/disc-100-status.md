# Original-disc 1.00 compatibility experiment

This is an experimental, hash-bound preparation/build profile, not a finished port.
Do not disable executable hash guards or apply the 1.02 native-adapter plan
to another executable. Existing 1.02 builds remain separate.

## Executable families verified locally

| Disc title ID | Language release | Decrypted executable |
|---|---|---|
| BCES00647 | Polish-content release | EU 1.00 family |
| BCES00644 | English, French, German, Italian, Dutch | Same EU executable, byte for byte |
| BCES00645 | English, Spanish, Portuguese | Same EU executable, byte for byte |
| BCUS98209 | USA | Separate adapted US 1.00 family; see [US status](us-100-status.md) |

EU 1.00 ELF SHA-256:
`6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8`.

Identical executable bytes do not prove that every region's assets, languages
or gameplay have been tested. Among EU discs, only the Polish-content assets have
been tested so far; the US disc has a separate validation record. The displayed
disc metadata version alone is insufficient.

## Verified progress (2026-10-29)

- The isolated reference build reaches Lua menus with the original 1.00
  executable and real SPURS jobs. No synthetic completion is used.
- Host video/decompression queue bindings are resolved through the executable's
  imports and Lua tables, rather than applying one global address offset.
- Virtual-buzzer callback TLS is initialized from the loaded ELF's PT_TLS
  template. It no longer assumes the 1.02 template address.
- The image36 geometry leaf is at 0x06C8 in 1.00 and 0x06D8 in 1.02. Its
  translated body was verified identical apart from its loop label. Both full
  generated-source hashes remain allowlisted; the shared body is hash-checked.
- The 1.00 native leaf passes 4,000 synthetic full-state cases and detects all
  five deliberately injected faults. The existing 1.02 suite also still passes.
- The running 1.00 game reaches three menu presentations and reports at least
  28,672 full-state leaf comparisons with zero mismatches. Twenty-four captured
  real leaf states also pass standalone reference/native comparison (100 repeats
  per captured state).
- Twenty-four real 1.00 image36 jobs were captured and replayed with reference,
  native and full-comparison modes. All three runs report 24 exact, zero
  input-divergent and zero failed jobs, covering 2,713 transfers. Full-comparison
  mode reports 24 leaf comparisons and zero mismatches.

## Migrated optimization set

- Completion polling and completed-slot queue: the 1.00 consumer/selector
  bodies are separately pinned. The native ABI test passes 12,000 states on
  **each** executable family, including callback draining, TOC/stack state and
  exactly-once retirement. Continuations are profile-specific, not shifted globally.
- Geometry: shared vertex, matrix-transform, normalization and triangle-cull
  algorithms, with explicit 1.00 input-register/constant/continuation mappings.
  The 1.00 vertex symbolic expressions retain the original arithmetic order.
- Image39/image40: checked, guarded native FP adapters against separately
  pinned lifts. Image40's local-register adapter also passes 256 in-game states.
- Video image7: the pure conversion leaf has its own 1.00 entry/exit/source pin;
  more than 140,000 in-game reference/native comparisons report no mismatch.
- MultiStream: firmware ATRAC and game DSP lifts use the 1.00 runtime-loaded
  code locations and exact code hashes. Dispatch still checks immutable LS
  bytes and retains interpreter fallback on an unknown module.
- Shared runtime/renderer work remains shared: worker dispatch, dependency
  wakeups, ready-list merging, caching and render fixes are not copied per version.
- The optional PPU flag wait site is bound to its reviewed 1.00 continuation.

All 64 captured 1.00 geometry jobs pass reference replay and checked native
kernel mode (341 transfers, no input divergence/output failure). In native mode,
30 repeats of each job also pass the externally observable job contract. In-game
checked mode reached at least 2,021,370 cull, 2,021,371 vertex, 2,572,015 transform
and 1,100,000 normalization comparisons with zero mismatches. These are kernel
contract comparisons, not a claim that dead volatile registers are identical.

The real ISO + PUP preparation pipeline has generated and compiled the full
1.00 build in an isolated workspace. The wizard now permits the three known
EU title IDs and preserves 1.02 as a separate family. The launcher identifies
the selected ELF and finds a matching prepared build; a manifest binds each
EXE to its source executable and runtime defaults. There is no forced default
version. Unknown hashes are rejected.

These correctness runs are **not an FPS benchmark** or evidence of stable
30 FPS. Full solo-round and other EU-language asset validation remain separate
work; [US validation](us-100-status.md) is recorded independently. The 1.02 Store-hiding Lua hook is not yet adapted to 1.00; this is a known
frontend limitation, not an omitted performance optimization.

## Developer entry point

`tools/build-disc100-reference.py --help` describes the isolated probe builder.
Supply your own matching decrypted ELF, extracted game and dev_flash. Generated
sources, executable and evidence stay under the ignored analysis directory.
The default developer probe uses reference SPU arithmetic. Standard preparation
selects the appropriate generation/native-adapter plan through the profile catalog.
In-place generation keeps host bindings under analysis, not in hand-edited
generated code or modifications to the user's disc image.

Remaining work: test each region's assets and a full solo round; measure controlled
FPS/pacing A/B runs; migrate the remaining frontend-only Lua hook.

## Gameplay reports and the startup-skip diagnostic

The user helped the test session enter a **two-player** round (not the intended
solo flow). Character models, Polish questions/answers and player names in the
score panel render. The user reports black avatar thumbnails, blank podium-name
displays and missing question photos. Do not claim those visuals work or that
question photos were previously verified in 1.02.

That test had `-SkipVideo`, whose old Lua hook suppressed video calls for the
entire session. The log shows PlayStream calls being blocked during questions,
well after startup. The hook now releases suppression on the first menu; a
synthetic test verifies later open/play/status calls reach the original code.
This is a concrete diagnostic-mode defect and a likely contributor to missing
question media, **not yet a verified in-game photo fix**. The existing generic
render-target subregion fix is present in the SDK, so the avatar/name issue
requires producer/consumer GPU captures rather than guessing address offsets.

The current prepared 1.00 build includes this startup-only fix and the corrected
observed VU-meter entry at LS 0x2F7F0. Native renderer captures can be triggered
without desktop focus in its private analysis/rt-check folder. The test uses
an isolated copy of the old save, never the original save directory.

### Avatar/podium capture: concrete atlas defects

A GPU capture of the user's two-player round shows that the producer works:
surface 0x060F0000 contains a rendered head and 0x06930000 contains "Adam".
The consumers sample **interior** addresses, not the surface bases: avatar
100x100 cells at +0x190/+0x320 and name 256x64 cells at +0x400/+0x800. Exact-base
RT lookup missed those and uploaded untouched CPU bytes instead. Moreover,
ordered CLEAR_SURFACE operations ignored scissor and erased the first player's
cell while clearing the second player's cell.

The SDK fix maps format/pitch/bounds-validated interior RGBA8 views to independent
GPU region resources, invalidates them by surface write generation, and clips
ordered color/depth clears to captured scissor state. There are no game-address
exceptions in the renderer. `test-rt-regions.ps1` covers layout/bounds/overflow
and independent cell clearing. Full 1.02 and prepared EU 1.00 builds compile
with the fix.

On 2026-10-29, after the user authorized closing the old session, the patched
EU 1.00 build was tested in **Standard game / two to four players**, using the
first two virtual handsets and the existing Babunia/Adam profiles in an isolated
save copy. Both podium names render simultaneously; both animated avatar cells
render in the score panel; a Countries picture question displays its windmill
and tulips photo, and both handsets can answer. Evidence is private under the
prepared workspace's analysis/rt-proof/captures (frame_16: both podium names;
frame_19: photo and both avatars; frame_20: both players' answers). The startup-only video
skip fix is therefore verified for this picture-question path, not every media
format or every game release. The real recorded atlas pitch is 5120 bytes for
both producer surfaces and their sampled views; do not infer it from width.

An opt-in BUZZ_TEST_INPUT_FILE mailbox drives the emulated handset report
directly for focus-free diagnostics. It accepts a monotonic sequence and a
20-bit handset mask, requires a complete newline-terminated record, and ignores
stale/duplicate/malformed writes. Normal launches do not enable it. Native GPU
captures, not desktop input/capture automation, were used for this test.

This correctness test is not an FPS benchmark or a claim of stable 30 FPS.

### Independent EU 1.02 multiplayer regression test

The 1.00 result was not extrapolated to 1.02. On 2026-10-29 the root checkout
was rebuilt against the update ELF (SHA-256
9303ccce9bc223ca6da2eb837256ef08be6f1f460dcc6d5191889de7a4eee241),
then tested separately in Standard game / two to four players with virtual
handsets P1 and P2. The existing Adam save was cloned into the ignored
analysis/rt-proof-102/gamedata tree; a second Babunia profile was created only
there (nurse and cheerleader characters). The original save's SHA-256 was
unchanged before/after the test.

Both podium names render simultaneously (captures/frame_21), both animated
score-panel avatars render, an archival film plays in a Cities question
(frame_24), and a second question displays the gondolas photo (frame_26).
Both handsets register their orange answer independently (frame_27). All
captures are native renderer output under analysis/rt-proof-102; startup-only
SkipVideo was used, and the log confirms real media calls resume at the menu.
The same generic atlas/scissored-clear fix handles this path in both releases;
no additional 1.02-only renderer change was required. This is not validation of
all rounds, all media formats, all character materials or FPS targets. The
test process was closed and launcher selection was left unchanged.
