# Frozen private Deck build, 2026-11-16

The private build reproduces the previously qualified executables byte-for-byte
without changing the shared prepared workspace. It is the working location for
further experiments after the [shared-build conflict](steam-deck-command-simd.md).
This is build isolation and reproducibility work, not an FPS improvement.

Private artifacts are in
`/home/deck/buzz/performance/optimization/codex-private-build/`.
The snapshot freezes the original link inputs, port/generated sources, SDK
headers and dependency headers. Every copied object/archive is checked against
its source before and after copying. The copied port C/C++ sources and headers
were also compared against the prepared workspace after copying, with no change.
Compilation and linking use recorded Ninja flags directly; shared CMake and
Ninja are never invoked.

Three exact executable reproductions establish the snapshot's provenance:

| Private artifact | SHA-256 | Meaning |
|---|---|---|
| `frozen-shared` | `9f244bf8e3c948d83e7eb5017b6f177e13c440f366cc3f908f551deb940cd763` | Copied link inputs reproduce the externally rebuilt, unqualified shared executable |
| `baseline-virtual` | `630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d` | Recompiling the restored geometry source reproduces the earlier prepared baseline |
| `reference` | `d6420f0b0905ed62958f6c42cfcbfe740d8851295484631d99460ccec49b5b56` | Recompiling the three retained image-7 translation units reproduces the qualified SIMD-video reference |

The current private object set corresponds to `reference`. No new gameplay
qualification is inferred from a merely similar binary: these are complete
SHA-256 matches to the archived executables. The unqualified shared executable
and installed distribution remain unchanged.

## Source paths affect profile use

The initial geometry rebuild in the relocated source tree produced a different
executable (`d384516a…`). `-ffile-prefix-map` preserved path literals but did not
preserve the profile identity of internal functions. PGO6 contains names such
as the original absolute source path followed by `;` and a local function name.
A different source path silently missed those records. Nine common text symbols
had different sizes; this was not a harmless embedded-path difference.

The successful rebuild uses Clang's `-ivfsoverlay` with
`use-external-names: false`. Original absolute source/header names map to frozen
private files, so both profile identities and path literals stay intact.
The resulting geometry rebuild exactly reproduces the old baseline. This is
stronger evidence than the absence of profile mismatch warnings alone.

A newly added translation unit absent at the original pathname is rejected by
the Clang driver before its frontend uses the overlay. The new image-7 SIMD
translation unit has no PGO input, so it is compiled at its real private path
with a file-prefix map. Existing profiled units continue to use virtual original
paths. That combination exactly reproduces the retained reference. The initial
failed attempt and its logs are preserved.

`freeze.py`, `virtual-rebuild.py`, `reference-build.py`, both Ninja snapshots,
`overlay.json`, object hashes, complete compiler/link commands, dependency output,
logs, intermediate executables and final verification are retained privately.
The first driver intentionally records a failed exact-hash assertion; later
successful stages have separate state files. Do not rerun `freeze.py` over the
existing snapshot, or use the failed relocated build as the reference.
