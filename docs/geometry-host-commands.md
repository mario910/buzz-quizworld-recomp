# Host geometry command construction and publication

`BUZZ_GEOMETRY_HOST_COMMANDS` replaces the image41 full and position-only command
groups with host packet construction. It is experimental and defaults OFF.

The existing whole-group admission validates the format, metadata, source and
destination spans, stack, aliasing and packet expansion before mutation. The
native writer then emits attribute bindings, metadata state, indexed batches,
leading batches, large batches and attribute resets using host integers and
direct big-endian stores. Publication splits packets at the same 128-byte
boundaries, shifts the same physical tails, appends semaphore packets and pads
the same reservation. It reuses the enclosing admission forecast when available.

Preparation retains its actual synchronization GET. Publication invokes the
same MachStat and ordered MFC argument/PUTF operations through the real runtime,
including validation, coherency, tag completion and wakeups. The caller's link,
stack and nonvolatile registers are restored at the same boundaries. A small
vector state preserves the complete live 128-bit result in r3. Volatile r2–r79
temporaries other than r3 are represented by host variables.

The writer allocates no new per-job buffers. The existing publication admission
simulator remains the fallback when preparation cannot forward a forecast.
Unsupported whole-group inputs retain the original implementation before any
native mutation. Strict stage diagnostics retain the previous SPU-ABI path,
whose contract includes the volatile temporaries those diagnostics inspect.

## Configuration

- CMake on Windows/Linux: `-DBUZZ_GEOMETRY_HOST_COMMANDS=ON` with the existing
  admitted native geometry profile.
- Windows helper: `-GeometryHostCommands`; omitting it explicitly resets OFF.
- Android: `-PbuzzGeometryHostCommands=true` with the existing NEON geometry
  profile. The property defaults false and reference29 explicitly keeps it OFF.
- Runtime: `BUZZ_HOST_COMMANDS=0` selects the previous implementation;
  `BUZZ_HOST_COMMANDS=1` or unset selects the compiled-in host path.
- `BUZZ_GEOMETRY_DEBUG_STAGES=1` uses the previous SPU-ABI path for strict stage
  comparisons. This choice is independent of the ordinary whole-job I/O proof.

Bind/reset/publication leaf callbacks are fused into this path, so their legacy
per-leaf counters can stay zero while whole command/job coverage grows. Native
preparation, index/vertex publication and whole-group admission counters retain
their existing meaning. The startup line reports the host-command runtime mode.
Select `BUZZ_HOST_COMMANDS=0` before bisecting individual legacy command leaves;
the host path owns binding, reset and publication as one group.
The fused publisher is scoped to the single synchronization record admitted by
these two enclosing command groups.

## Validation

The pinned original 74-block full and 75-block position command graphs are
independent oracles. On Windows clang-cl, Linux x86 and ARM64/QEMU, 8512 full and
8320 position cases pass comparisons of every LS byte, all architectural controls,
ordered channel inputs/arguments/transfers, transferred payloads, return PC,
link/stack/r3 and nonvolatile registers. The conditional host proof excludes only
volatile r2–r79 other than r3. Original tests keep their strict full-register
proof when the host variant is not selected.

Cases include arbitrary non-preferred register lanes, multipass metadata,
masked/empty attribute lists, all alignment residues, minimum reservations,
packet splits and physical padding, null publication destinations, large batch
splitting and unsupported-input rejections. The original retained x86 full and
position suites also pass unchanged. Native preparation cases check that the
single-use publication forecast is forwarded successfully. The extended ARM64
driver runner also passes its driver-call/exit, index/vertex publication and
retained command fixtures with the new header included.

Windows test helpers select the host variants with `-HostCommands`:

```powershell
./tests/scripts/test-geometry-full-commands.ps1 -HostCommands
./tests/scripts/test-geometry-position-commands.ps1 -HostCommands
```

`tests/scripts/test-geometry-driver-arm64.sh` includes the retained and host
variants. These oracle tests require the prepared pinned retail geometry source;
generated game source and captured game data remain private.

A 60-second locked-console smoke run renders the expected animated menu with
the new path enabled. Hardware FPS/CPU qualification is separate from oracle
correctness. Android hardware performance and question/answer/scoring progression
have not been qualified by these tests. Stable 45 FPS remains an open goal.

## Matched PC FPS qualification

Six 100-second runs use the same private save seed, scripted inputs, shader cache,
1024x576 client extent, 1280x720 render extent, mailbox mode and two contexts.
All executable/profile/window/locked-session guards pass; all six 35-second
Vulkan readbacks show the intended menu without an obvious defect. No build,
correctness test, sampling profiler or second game overlaps the timed cohort.
The incremental control is the frozen index-cache candidate; other geometry,
renderer, PPU and pool50 choices stay matched. Enabling this option alone does
not reproduce the complete experimental bundle.

| Build | Heavy 45–60s FPS, two runs | Pooled 45–90s FPS |
|---|---|---|
| Same immutable reference14 | 37.5884 / 37.7308 | 44.9609 |
| Frozen index-cache control | 39.3893 / 38.5108 | 47.5940 |
| Host command candidate | 40.6190 / 40.1621 | 48.9269 |

The incremental gain is about 1.44 FPS in the heavy window and 2.80% in the
longer window. Both candidate heavy runs exceed both control runs, though this
is a two-repeat workload comparison. Longer-window candidate P99 frame times
are 30.799/30.817ms; about 42.2% of intervals exceed 22.222ms. The higher pooled
average does **not** demonstrate stable 45 FPS. These measurements cover an
animated menu; they do not establish question/answer/scoring or Android speed.

Frozen executable SHA256 values:

- reference14: `d66601b4c612eb4006597d285e9d247ed538692d269cadb1448219179fb903fb`
- index cache: `0c478568f2a02c658312984a9f6b2fb68e742654070792e76903c8cf32f6dfa8`
- host commands: `1cdddd62a3e93bca31bdb7ee95e32e2fb476bf15509b885ff946447de55662a3`

Raw runs, readbacks and profile/session checks are retained privately under
`analysis/pc45-image39/commands-*`. Separate CPU attribution uses runtime
host commands OFF/ON/ON/OFF on the same host-command EXE, with index cache ON
throughout. Four separate 72-second CPU runs pass all hash/profile/window/session
guards. The 45–65s intervals use owned persistent process/thread handles, with
no suspension, timed thread enumeration or sampling profiler.

| CPU measurement | Host commands OFF | Host commands ON | Reduction |
|---|---|---|---|
| Process CPU ms/frame | 131.8491 | 128.4371 | 2.59% |
| Process cycles/frame | 487.663 million | 474.478 million | 2.70% |
| SPURS pool CPU ms/frame | 61.8418 | 60.0104 | 2.96% |
| SPURS chain CPU ms/frame | 22.7125 | 22.2763 | 1.92% |
| Process busy cores, uncapped | 5.3994 | 5.4009 | effectively unchanged |

Per-run process CPU ms/frame: OFF 130.7495 / 132.9459, ON 129.6819 / 127.1923.
RSX time stays approximately unchanged (15.4915 to 15.4485 CPU ms/frame).
Estimated non-game load is 2.89–2.95 busy cores in OFF runs and 3.09–3.15 in
ON runs. The uncapped game produces more frames with nearly the same total CPU
utilization. These measurements establish less CPU work per frame; electrical
power and capped-load savings on Steam Deck/Android require hardware evidence.

CPU raw groups `commands-cpu-previous`/`commands-cpu-power` mean runtime OFF/ON
on the **same** candidate EXE. FPS `commands-previous` is the **separate** frozen
index-cache EXE. Their denominators are intentionally different and must not be
interchanged. The feature remains OFF by default and in Android reference29.
