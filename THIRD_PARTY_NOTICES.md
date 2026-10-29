# Licensing and third-party notices

The root MIT license covers this project's port code, generators, tests and
documentation, including scaffolding adapted from ps3recomp. It does **not**
license Buzz! Quiz World, Sony firmware, locally generated translations of
either, or the following separately licensed components.

| Component | License / retained notice | Use |
|---|---|---|
| [ps3recomp](https://github.com/sp00nznet/ps3recomp), Ned Heller / sp00nznet and contributors | `ps3recomp/LICENSE` (MIT), except separately licensed components | Runtime, lifters, port scaffolding; our normal fork `master` is pinned as a submodule; inherited-history caveat in `docs/publication.md` |
| zlib, Jean-loup Gailly and Mark Adler | `third_party/zlib/LICENSE` (zlib) | Native decompression |
| FFmpeg ATRAC3/ATRAC3plus decoders, extracted via PPSSPP | `ps3recomp/third_party/at3_standalone/COPYING.LGPLv2.1`, per-file notices and `README.txt` (LGPL-2.1-or-later; some shims have their own notices) | SDK codec component; compiled into the runtime |
| stb_image, Sean Barrett and contributors | license text inside `ps3recomp/libs/codec/stb_image.h` (MIT or public domain) | SDK image decoding |
| stb_truetype, Sean Barrett and contributors | license text inside `ps3recomp/libs/font/stb_truetype.h` (MIT or public domain) | SDK font support |
| pycdlib, Chris Lalancette and contributors | LGPL-2.1-only; [upstream license](https://github.com/clalancette/pycdlib/blob/master/COPYING), also installed with the pip distribution | ISO/Joliet/UDF import tool, not vendored source |
| RPCS3 | GPLv2; [upstream license](https://github.com/RPCS3/rpcs3/blob/master/LICENSE), consult the separately supplied distribution | Optional local preparation helper for PUP/PKG/SELF; not redistributed or linked into this port |

Other notices in upstream files remain in place. Tool dependencies installed
by pip have their own licenses; they are not vendored by this repository.

## Source publication, not a redistributable game build

No game, update, firmware, decryption keys, generated PPU/SPU translations,
captured game jobs, textures, music, fonts, quiz data or native game EXE are
part of this source release. Generators require locally supplied inputs and
write derived output into ignored directories. Native kernels and adapters
are hand-written compatibility/optimization implementations; this is not a
claim of a legally reviewed clean-room development process.

Do not upload your `build-windows`, `port/recompiled`, `port/spu`, `analysis`,
`runtime`, game/firmware folders or logs as a release. A built EXE contains
translated game/firmware code, not just this project's MIT source. In addition,
distributing a binary linked to the LGPL decoder requires a separate LGPL
compliance review, including the applicable source/relinking requirements.
Source publication is not authorization to redistribute game-derived builds.

Owning a disc is not a blanket exception to copyright or anti-circumvention
law. Use files you are entitled to use, follow the rules in your jurisdiction,
and obtain legal advice if you need a legal opinion. This project does not
supply game/update/firmware downloads or fetch per-disc/license keys. The
preparation wizard can invoke the user's separate RPCS3 installation to import
their firmware/update and prepare a local executable; this does not change the
rights associated with those inputs or the generated output.

Buzz!, PlayStation and other product names belong to their respective owners.
This is an unofficial project, not endorsed by Sony, Relentless Software or
the ps3recomp upstream maintainers.
