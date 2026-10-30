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
| SDL3, SDL contributors | zlib; pinned source `LICENSE.txt`, installed as `licenses/SDL3.txt` | Native launcher window, input, dialogs and direct process API; statically linked |
| Dear ImGui, Omar Cornut and contributors | MIT; pinned source `LICENSE.txt`, installed as `licenses/ImGui.txt` | Native launcher UI |
| JSON for Modern C++, Niels Lohmann and contributors | MIT; pinned source `LICENSE.MIT`, installed as `licenses/nlohmann-json.txt` | Native launcher settings/manifest parsing |
| PicoSHA2, okdshin and contributors | MIT; pinned source `LICENSE`, installed as `licenses/PicoSHA2.txt` | Native launcher's SHA-256 consistency checks |
| cpp-httplib, Yuji Hirose and contributors, v0.18.7 | MIT; `third_party/cpp-httplib/LICENSE` | Embedded local HTTP phone-controller server |
| QR Code generator, Project Nayuki, v1.8.0 | MIT notice in each file in `third_party/qrcodegen` | In-game phone pairing QR code |

Other notices in upstream files remain in place. Tool dependencies installed
by pip have their own licenses; they are not vendored by this repository.

The built-in PS3 importer uses PyCryptodome (BSD/public-domain components;
[upstream license](https://www.pycryptodome.org/src/license)) for AES, Python's
standard library for hashing/TAR/zlib, and the MIT SDK's existing PKG keystream
implementation. SELF/PUP parsing is implemented from public file-format
descriptions linked in `docs/game-import.md`; no RPCS3/scetool code or key
database is included. Those external programs are not needed for preparation.

The optional Vulkan build additionally fetches pinned Khronos sources:
Vulkan-Headers (Apache-2.0 OR MIT; see its `LICENSE.md`), glslang (the component
licenses collected in its `LICENSE.txt`), SPIRV-Headers (MIT) and SPIRV-Tools
(Apache-2.0). Versions/archive hashes are recorded in the SDK's
`cmake/VulkanDependencies.cmake`; the SPIR-V pair matches glslang's known-good
revision list. Their shader libraries are linked into experimental binaries,
not downloaded or launched while playing. Preserve their upstream notices
when packaging a binary; the native launcher's own dependency set is unchanged.

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
preparation wizard imports locally supplied firmware/update files and prepares
a local executable in-process; this does not change the
rights associated with those inputs or the generated output.

Buzz!, PlayStation and other product names belong to their respective owners.
This is an unofficial project, not endorsed by Sony, Relentless Software or
the ps3recomp upstream maintainers.
