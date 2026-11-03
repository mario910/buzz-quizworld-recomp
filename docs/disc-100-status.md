# EU original-disc profiles

Preparation selects an executable by exact hash, not by a title's displayed
version. Do not apply an EU 1.02 adapter to another executable or disable the
hash guards. The US profile is described [separately](us-100-status.md).

| Title ID | Content release | Executable family |
| --- | --- | --- |
| BCES00647 | Polish | EU 1.00 |
| BCES00644 | English, French, German, Italian, Dutch | Same EU 1.00 executable |
| BCES00645 | English, Spanish, Portuguese | Same EU 1.00 executable |

EU 1.00 ELF SHA-256:
`6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8`.
Only Polish EU assets have been exercised locally. Identical executable bytes
do not establish validation of every release's languages/assets.

## Version-bound adapters

- Completion consumer/selector bodies and their continuations are pinned per
  executable family. Native completion retains callback draining, TOC/stack
  state and exactly-once retirement.
- Host video/decompression queues resolve through imports and Lua tables,
  rather than a global address delta. Buzzer callback TLS comes from PT_TLS.
- Shared SPU algorithms retain their own input-register, constant and
  continuation mappings. The image36 leaf is at `0x06C8` in EU 1.00 and
  `0x06D8` in EU 1.02; the shared body is hash-checked.
- Image39/40 native FP and video image7 have guarded, source-bound adapters.
  Firmware ATRAC/game DSP modules use runtime code locations and immutable
  LS hashes; unknown modules retain interpreter fallback.
- Worker dispatch, dependency wakeups, caching and renderer fixes are shared
  runtime behavior, not per-version copies.

The wizard generates a matching preparation session. A manifest binds its
EXE to the source ELF and runtime profile. Unknown hashes are rejected, and
generation from one family cannot be resumed with another family's input.
`tools/codegen/build-disc100-reference.py --help` describes the isolated
reference probe; ordinary preparation selects the supported native adapters.

## Media and render-target behavior

The startup-skip diagnostic releases its Lua suppression at the first menu;
later question videos and picture sheets must reach the original media path.
A whole-session video bypass can hide question media and is not equivalent to
skipping only startup films.

Avatar and name consumers can sample an interior render-target address.
The renderer resolves format/pitch/bounds-validated RGBA8 subregions to GPU
views, invalidates them by surface generation and honors captured scissor
state for ordered clears. It must not upload untouched CPU bytes merely
because the sample address differs from the surface base. Atlas pitch comes
from the producer, not from cell width. `tests/scripts/test-rt-regions.ps1`
checks region layout, bounds and independent cell clearing.

Polish EU gameplay has exercised two-player names, animated avatars,
questions/pictures and scoring. This does not establish every round/media
path, other EU-language assets, long sessions or stable frame rates. The
Store-hiding Lua hook is not adapted to 1.00; a visible Store item does not
mean PSN is supported.
