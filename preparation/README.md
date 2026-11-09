# Native game preparation

`BuzzPrepare` is the C++17/Qt Core implementation of preparation primitives.
It executes no Python or external decryption/extraction program. Windows AES
uses system BCrypt; Linux AES uses OpenSSL Crypto. SHA/HMAC are implemented
through Qt Core, and section decompression uses the pinned zlib submodule.

Open `BuzzSetup.exe` on Windows or `BuzzPrepare` without arguments on Linux
for the graphical wizard. Full game
preparation targets Windows and Linux x86-64. The wizard checks the host and offers
dependency installation, accepts local game and firmware inputs, then uses
the Python import and generation pipeline, with PowerShell build commands on Windows, to build a
standalone Vulkan distribution in `dist/` beside the build sources. Native
command-line import and analysis remain available on Windows and Linux.
UDF-only discs still need a native reader; the native CLI supports
ISO9660/Joliet bridges, including hybrid UDF images with a readable bridge.

**Prefer the original-disc EU or US 1.00 for normal preparation.** These are
the primary optimization targets. Leave **Game update (optional)** empty in
the wizard, or omit `--update` on the command line. EU 1.02 remains a separate
compatibility profile and does not receive the full 1.00 native-job profile.
The full EU/US geometry and animation path is qualified on Windows/Vulkan/AVX2;
Linux and Android have separate profiles. See the [main build and play guide](../README.md)
for complete 1.00 preparation and startup commands.

## Native operations

```
BuzzPrepare --inspect-iso <game.iso>
BuzzPrepare --extract-iso <game.iso> --out <new-game-directory>
BuzzPrepare --decrypt-elf <EBOOT.BIN> --keys <local-key-file> --out <EBOOT.elf> --title-id BCES00647
BuzzPrepare --extract-firmware <PS3UPDAT.PUP> --keys <local-key-file> --out <import-directory>
BuzzPrepare --extract-update <update.pkg> --out <update-directory> --title-id BCES00647
BuzzPrepare --validate-firmware <dev_flash>
BuzzPrepare --import-game <game.iso-or-dump> --firmware <PUP-or-dev_flash> --keys <local-key-file> --out <new-session>
BuzzPrepare --analyze-elf <EBOOT.elf> --out <analysis-directory>
BuzzPrepare --extract-spu <EBOOT.elf> --out <spu-image-directory>
BuzzPrepare --decode-spu <SPU.elf>
BuzzPrepare --lift-spu <SPU.elf> --out <source-directory>
BuzzPrepare --lift-spu-workloads <image-directory> --out <source-directory>
BuzzPrepare --lift-spu-raw-jobs <game-elf> --title-id <disc-id> --out <source-directory>
BuzzPrepare --lift-multistream <game-elf> --title-id <disc-id> --firmware <dev_flash> --out <source-directory>
BuzzPrepare --generate-hle-nids <ps3recomp-sdk> --out <ppu_hle_nids.cpp>
BuzzPrepare --decode-ppu <raw-executable-segment> --ppu-base <address> --out <instructions.tsv>
BuzzPrepare --find-ppu-functions <game-elf> --out <functions-full.json>
```

For native sessions, add `--update <PKG-or-SELF-or-ELF>` to select an update, and
`--resume` with the same `--out` and inputs to resume explicitly. Imported output
and the JSON ledger are kept in that directory. Source inputs are read-only.
Input identities, completed output fingerprints and a per-session lock protect
resume. Failed stages do not mark the import complete. Firmware can recover a
publication interrupted before its ledger was saved. ISO extraction publishes
the whole directory atomically and verifies an already-published directory on
recovery. The generated ELF must match a supported title/hash before publication.
Key files are read locally; their values are never put into logs or ledgers.

CLI results are JSON on stdout; progress goes to stderr. Import-only completion
is deliberately separate from generation/build completion. No `last-ready.json`
is written by this implementation stage.

## Developer build

Initialize `ps3recomp` and `third_party/zlib`. Windows requires MSVC, CMake/Ninja
and Qt 6.2+ Core/Widgets/Test; `scripts/build-preparation.ps1 -QtPrefix <Qt MSVC prefix>`
configures, builds and runs native tests. The developer helper can use a preexisting
tool environment for CMake/Ninja, but the resulting binary does not need it.
The helper deploys `dist-preparation/bin/BuzzSetup.exe` as a Windows GUI
application and `dist-preparation/bin/BuzzPrepare.exe` for native CLI commands.

## Windows release package

The **Native preparation** workflow tests the wizard and packages it with the
committed build sources and pinned submodules. Pushing a new `v*` tag publishes
the tested Windows ZIP, SHA-256 checksums and matching Qt source archive as
GitHub Release assets. Non-tag runs expose the Windows package as a workflow
artifact. The package uses shared Qt 6.8.3 libraries, and its `licenses/Qt/`
directory contains the upstream license texts. The Qt DLLs can be replaced
with compatible versions built from the corresponding source.

Open `BuzzSetup.exe` at the top of the extracted package. Its bundled sources
and private build output are under `build-files/`.

To reproduce the package locally, build with the official Qt 6.8.3 MSVC
2022 x64 distribution from the commit you want to package. Download
[qtbase-everywhere-src-6.8.3.tar.xz](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz)
and run:

```powershell
python tools/package-preparation.py --qt-source <path-to-qt-source-archive> --output dist-preparation/release
```

The release contains only BuzzPrepare, its support libraries and build sources.
The game and firmware translations are built privately on the player's PC.

## Linux developer build

On Ubuntu 22.04/24.04 install `build-essential cmake ninja-build qt6-base-dev
libssl-dev`, then run `bash scripts/build-preparation-linux.sh`. The installed
wizard is `dist-preparation/linux/bin/BuzzPrepare`. The Linux GUI needs Qt 6
Widgets and OpenSSL 3 at runtime. Its package buttons cover APT, DNF and pacman,
with a terminal command available when graphical elevation is unavailable.
Install the appropriate Vulkan driver for your GPU through your distribution.

For headless preparation on Debian or Ubuntu, install `build-essential clang
patch git python3-venv pkg-config libsdl2-dev libvulkan1` and a Vulkan driver.
Initialize the submodules, then run:

```sh
python3 tools/setup-linux.py
./toolenv/bin/python tools/prepare-game.py --dist --game /path/to/game.iso \
  --firmware /path/to/PS3UPDAT.PUP --keys /path/to/ps3.keys
./dist/BuzzQuizWorldRecomp
```

Linux uses SDL2 for the window, keyboard, game controllers and original Buzz
receivers exposed as compatible joysticks. Phones and the Vulkan settings menu
use the same game code as Windows. `--upscaler` installs the pinned optional
Linux Real-ESRGAN executable and models.

Tests use frozen, authored binary fixtures. They cover signed SELF with all three
supported digests, free-license NPDRM, unsigned SELF, compressed sections, PUP
header/content authentication, retail/debug PKG, unaligned multi-chunk payloads,
ISO9660/Joliet/hybrid bridges, path collisions/traversal/symlinks, cancellation,
resume, input/output changes, publication recovery and redacted key-file errors.
Synthetic keys are arbitrary byte sequences. Python is used only to regenerate
reference fixtures, never by the native tests or application.

The native ELF analyzer writes the four SDK loader metadata documents (load
segments, OPD/function seeds, entry/TOC and firmware imports). Embedded SPU
extraction includes the entire runtime fingerprint extent; the decoder preserves
the SDK's opcode precedence and operand spelling. The fixtures check loader
output and 16,384 instruction cases over every 11-bit opcode prefix.

Real local verification: all 51 files (7,922,200,722 bytes) extracted from the
Polish EU 1.00 ISO matched the previous import by SHA-256. SELF decryption on
Windows and Linux produced the expected pinned EBOOT. The packaged Windows
import and resume succeeded with PATH restricted to Windows system directories.
The four PPU metadata documents matched the SDK reference, including 8,868
function seeds and 326 imports. All 8 embedded SPU ELF extents, runtime
fingerprints and 82,540 decoded instructions matched the reference.

Native SPU generation discovers function bounds, emits C and headers and
registers workloads by full-image and executable-segment fingerprints.
For the 8 EU 1.00 images, the function bounds, generated code and workload
registration entries match the SDK reference across 3,854 functions.
The generated translation units pass the C compiler's syntax check against
the runtime headers.

Raw SPURS jobs are generated only for the pinned EU/US 1.00 and EU 1.02
executables. The disc profiles check the job layout, entry overrides and
registration budget. The update profile checks captured payload fingerprints.
For EU 1.00, all 16 generated C/header pairs and the ordered registration
entries match the Python reference. Windows and Linux emitted identical
outputs, and the C compiler accepted all 17 translation units. The US 1.00
and EU 1.02 paths still need comparisons with their original inputs.

MultiStream generation verifies the game profile and each module's .text
SHA-256, lifts the modules at their reviewed local-store addresses and
preserves the mutable ATRAC window in the runtime fingerprint. For EU 1.00,
ATRAC, compressor and vumeter code and registrations match the Python
reference on Windows and Linux.

The HLE generator scans SDK modules and computes NIDs from the exported names.
All 1,204 SDK registrations match the Python reference. The game's CMake
configuration accepts `-DPORT_PREPARATION_EXECUTABLE=<absolute BuzzPrepare path>`
to use the native generator if the lift has no HLE table.

The native PPU decoder preserves the SDK's opcode precedence, mnemonic
aliases, operands and comments. The 2,147,864 words in the EU 1.00 executable
segment and 250,000 random words at high addresses matched the SDK decoder.
The EU 1.00 diagnostics were identical on Windows and Linux.

PPU function discovery combines OPD and loader seeds with prologue scans,
call targets and control-flow extent growth. It preserves the SDK's leaf scan
order because earlier discoveries can change where a later scan stops.
All 21,458 EU 1.00 start/end ranges matched the SDK's functions-full.json
and the native Windows and Linux output files were identical.

The PPU scalar lowering code emits C++ statements for integer and
floating-point operations, memory accesses, atomics, special registers and
system calls. The EU 1.00 executable segment yielded 1,654,055 matching
statements, and 180,625 additional statements matched in a random-word
corpus. The CTest suite covers TOC restoration and condition-record forms.

PPU vector and branch lowering, function emission, adapter generation and
integration into the preparation and build entry points are in progress.
