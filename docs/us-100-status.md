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
