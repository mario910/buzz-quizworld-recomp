# US original-disc profile

Supported title: BCUS98209, original US 1.00. Decrypted ELF SHA-256:
`fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d`.
An EU executable, an unreviewed US update or a title ID alone cannot select
this profile. Use normal preparation with the user's own disc/ISO.

## Shared and version-specific code

All 16 raw SPU job spans and eight embedded SPU programs match the reviewed
EU 1.00 images byte for byte. Extraction records actual US offsets and
validates full image hashes before assigning canonical role names. The
shared SPU algorithms do not imply a common PPU address layout.

The full host geometry (Image41) and animation (Image40) paths now also
build for US Windows/Vulkan/AVX2. Extra SPU continuation entries are selected
by canonical role and full-program hash, including entry-word checks, for
both discs. All 24 generated SPU translation units match the EU disc profile;
the existing geometry/animation adapter pins are unchanged. US PPU code is
not replaced with EU code.

The EXE and manifest use `us100-vulkan-native-v1`. The native launcher and
developer runner validate its US family/ELF hash; CMake additionally rejects
this label with a non-US PPU completion adapter (and the inverse EU label
with the US adapter). Explicit runtime overrides still take precedence.

PPU adapters resolve Lua bindings from the loaded image, queue sites from
firmware import calls and TLS from PT_TLS. The completion/selector bodies
at `0x2689D0` / `0x26A2BC` in chunk 014 are independently pinned. Native
completion preserves BUSY/error/success and exactly-once retirement.
The manifest binds the EXE and defaults to this ELF; partial generation
cannot resume with another executable.

The ISO importer supports the large question PAK's multi-record ISO9660
continuation chain. It sums bounded records and rejects cycles, overruns
and an extracted size different from the declared total.

## Validation scope and limitations

Local tests cover US import/preparation, menus, profile/character creation,
two-player input, names/avatars, text questions, scoring and transition to
the next standard round. They use a private US save root and do not modify
EU saves. Question-image/video playback, a complete unskipped intro, every
round/media file, US updates and stable frame rates are not separately
qualified. Online/MyBuzz remain unsupported, and the 1.00 Store-hiding hook
is not adapted.

### Full native job validation (2026-11-08)

A fresh US build passed launcher identity validation and a bounded Windows
Vulkan solo challenge run: studio/menu, existing US profile, Buzz's entrance,
character animation, avatar/podium name, text questions and transitions.
Audio rendering remained enabled but volume was zero. The save was copied
into a separate test runtime; neither the original US save nor EU saves
were changed.

737 sampled jobs from that run (372 animation, 365 geometry) replayed with
**zero mismatches and zero concurrent-input divergences**, preserving all
4561 ordered transfers in retained, native and paired comparison modes.
The native geometry path executed 319 jobs and used whole-job fallback for
46; paired mode compared all 319. All 372 animation jobs executed natively
and passed paired comparison. This is sampled job-boundary validation, not
proof of every input, round, media file or long-session stability. The new
full-job build has not yet repeated the earlier two-player/scoring test or
established a matched US FPS improvement.

The new launcher/profile tests pass. The unrelated existing `pc-settings`
test still fails because its expected menu row counts predate the current
menu model (13/10 versus 16/12); it was not weakened by this migration.
