# Continuous integration

Public CI builds developer tools and synthetic fixtures only. It must not import
or upload retail ISO files, decrypted game executables, generated game-code
translations, firmware, question databases, saves, keys or local runtime folders.
Use your own supported game/firmware locally for a complete game build.

## Buzz repository

| Workflow | Coverage |
|---|---|
| Native preparation | Windows x64 and Ubuntu 22.04/24.04: compiled importer, synthetic import/lifter tests; stages the preparation application with Question Studio and smoke-tests both from the extracted packages |
| Question Studio | Windows x64 and Ubuntu 22.04/24.04: editor, synthetic pack/document tests and offscreen widget smoke tests |
| PC port host tests | Windows x64, Ubuntu 24.04 x64 and macOS 15 arm64: input routing, settings, phone sessions/server, host controls/overlay, game paths and PC runtime (`tests/CMakeLists.txt`) |
| macOS ARM64 preview | `feature/macos-arm64`: builds and smoke-tests BuzzSetup and Question Studio bundles, links the Metal game host against synthetic guest functions, and uploads the development ZIP to the workflow run |
| Optimization catalog | Checks the source-derived optimization documentation |
| Workflow validation | actionlint and ShellCheck over workflow YAML/embedded shell |

The host tests require CMake 3.31 or newer. Their CI pins the build tools and
checks out the exact SDK submodule commit; SDK or shared input/settings changes
trigger them too. Passing macOS tests do **not** establish a working macOS game,
graphics, audio or performance.

Artifacts contain installed tool binaries and their licenses, not the checkout
or build tree. Linux preparation archives currently require the matching host's
Qt/OpenSSL libraries; they are not self-contained game releases. Artifacts are unsigned development builds.

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
