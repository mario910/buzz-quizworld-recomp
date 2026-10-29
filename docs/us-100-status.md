# US original-disc support (BCUS98209)

Supported executable: original US 1.00, decrypted ELF SHA-256
`fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d`.
An EU executable, an unreviewed US update or the title ID alone does not select
this profile. Prepare the user's own ISO/dump through the normal wizard.

## Reuse without address guessing

All 16 raw SPU job spans and all 8 embedded SPU ELF programs are byte-identical
to the reviewed EU 1.00 images. US extraction records actual container offsets
and validates full embedded-image SHA-256 values before assigning canonical
tooling-role names. These names are not claims about US offsets. All 24
generated SPU translation units then match EU 1.00 exactly, including the
existing pinned geometry, image36/39/40, video and mixer adapters.

PPU code is different and is **not** relocated by a global delta. Lua bindings
are resolved from the loaded image, queue publication/initialization sites from
firmware import calls, and callback TLS from PT_TLS. Completion at 0x2689D0
and selector 0x26A2BC have independently pinned US reference bodies in chunk 014.
The shared native completion adapter passes 12,000 full-context/stack/callback
cases against that US reference, including BUSY/error/success and exactly-once
retirement. The generated manifest binds the EXE and runtime defaults to the US
ELF, and partial disc generation cannot be resumed with another executable.

## Local validation (2026-10-29)

- The real US ISO imported and completed the standard preparation/build path.
  Its large question PAK spans multiple ISO9660 records. The importer now sums
  the bounded continuation chain, rejects cycles/size overruns and verifies the
  actual extracted size. Synthetic fixtures cover this path and rejection cases.
- US menus and fresh profile/character creation work with the first two virtual
  buzzers. Standard game / two to four players reaches English questions.
- Both podium names and animated score-panel avatars display. Both players'
  answers register and award 250 points on a correct answer.
  The first standard round completed and the next round loaded successfully.
- A separate checked-geometry run reached at least 806,004 cull, 806,006 vertex,
  1,000,000 transform and 428,048 normalization comparisons with zero mismatches.
  Gameplay testing then used the normal native mode, not comparison mode.
- Tests use a private US save root and application-native input/captures, not
  desktop focus or the user's EU saves. Private evidence is ignored by Git.

This is not validation of every round, every media file, US updates or stable
30 FPS. Online/MyBuzz remain unsupported in this source release. As in EU 1.00,
the Store-hiding Lua hook is not adapted, so its menu item can remain visible.
Startup films were skipped for the diagnostic gameplay run; later media calls
use the original video path. The selected US questions in this run were text-only,
so US question-image/video playback is not claimed as separately verified.
Testing was stopped at the user's request after accepting this initial scope.
