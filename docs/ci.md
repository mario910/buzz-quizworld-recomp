# Continuous integration

Public CI builds developer tools and synthetic fixtures only. It must not import
or upload retail ISO files, decrypted game executables, generated game-code
translations, firmware, question databases, saves, keys or local runtime folders.
Use your own supported game/firmware locally for a complete game build.

## Buzz repository

| Workflow | Coverage |
|---|---|
| Native preparation | Windows x64 and Ubuntu 22.04/24.04: compiled importer, synthetic import/lifter tests; stages only the preparation application |
| Question Studio | Windows x64 and Ubuntu 22.04/24.04: editor, synthetic pack/document tests and offscreen QML smoke tests |
| Native launcher and PC settings | Windows x64, Ubuntu 24.04 x64 and macOS 15 arm64: SDL3/ImGui launcher, input routing, settings, phone sessions, host controls and hidden UI tests |
| Optimization catalog | Checks the source-derived optimization documentation |
| Workflow validation | actionlint and ShellCheck over workflow YAML/embedded shell |

The launcher requires CMake 3.31 or newer. Its CI pins the build tools, fetches
hash-pinned SDL3/ImGui dependencies and checks out the exact SDK submodule commit.
SDK or shared input/settings changes trigger launcher tests too. Apple Silicon
artifacts are labelled **bootstrap**: a compiled launcher and passing synthetic
tests do **not** establish a working macOS game, graphics, audio or performance.

Artifacts contain installed tool binaries and their licenses, not the checkout
or build tree. Linux preparation archives currently require the matching host's
Qt/OpenSSL libraries; they are not self-contained game releases. The standalone
launcher artifact uses statically linked SDL3, but still needs normal system
graphics/windowing libraries. Artifacts are unsigned development builds.

## SDK fork

The SDK retains its Windows, Linux/GCC+Clang, Linux/Vulkan and Apple Silicon/Metal
checks. These exercise manufactured programs, not Buzz disc data. Shader/graphics
and architecture results belong to the runner and do not qualify a full game
port. Release packaging is separate and only the publishing job gets write
permission; ordinary checks have read-only repository access.

Both repositories pin external actions by full commit, use explicit runner
versions and bounded jobs. Ordinary branch checks cancel superseded runs. The
source-only checks intentionally do not run the private game integration scripts.

For a failed job, inspect its first actual compiler/test error, not just the
final workflow exit code. Do not hide regressions with `continue-on-error` or
remove fixtures to obtain a green run. A missing graphics capability should be
reported explicitly rather than presented as a successful graphics test.
