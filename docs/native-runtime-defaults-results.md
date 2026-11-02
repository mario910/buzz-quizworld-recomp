# EXE-owned native runtime defaults

2026-11-02. Decision: the qualified EU100/Vulkan/AVX2 executable owns the
approved runtime profile. An absent environment variable selects its reviewed
default; explicit values, including `0` and diagnostic modes, take precedence.
Launchers only supply paths, input and preferences for this profile; legacy
frozen builds retain their existing launcher defaults.

| Area | Default |
| --- | --- |
| Image 41 host job | Native; host SoA, publication and commands enabled; unsupported jobs use the whole lifted job |
| Image 40 host job | Native; unsupported descriptors retain whole-job fallback |
| Earlier native methods | Stage F mode 1, image 39 native FP, image 7/36 mode 1, geometry mode 1 with reviewed mask 827 (`0x33B`) |
| Completion / scheduling | Completion mode 2, completion queue, four workers, cached options, sequence/wake/merge improvements |
| Vulkan | Reviewed batching/ring/passes/clears, asynchronous draw, mailbox, texture reuse and native texture rows |
| Unqualified builds / experiments | No new embedded profile; rejected experiments and diagnostic captures stay opt-in |

The 48-entry profile is maintained in `port/runtime_defaults.inc`. The EXE
installs it before C/C++ constructors cache environment options. Compile-time
native FP, RTZ guard, ISA and hot-code flags remain bound to the qualified
build. `--runtime-defaults` reports effective values without booting the game.
Older individual geometry toggles belong to the retained legacy implementation;
the host pipeline replaces that implementation rather than enabling both.

| Validation | Result |
| --- | --- |
| Constructor initialization | Enabled/disabled profiles × absent/0/2/mixed overrides; all 48 options visible in C and C++ constructors and main |
| Actual bare EXE | All 48 defaults present without launcher; real Stage F, geometry, image 7/36 constructors see native mode; all 48 explicit OFF values preserved |
| Image 40, absent animation option | 17,714 exact jobs / 115,162 transfers; 17,714 native, zero fallback or skipped audit |
| Image 41, absent geometry option | 19,508 exact jobs / 58,195 transfers; 17,437 native, 2,071 existing fallbacks, zero failures |
| Mixed corpus | 20 exact jobs in each default, explicit OFF and native-retained diagnostic arm |
| Launchers | Native tests cover every profile option absent and overridden; legacy compatibility, profile qualification and PowerShell manifest guards pass |
| Actual launch plans | GUI default/OFF plans select the new EXE; PowerShell process-boundary test preserves all 48 absent options and explicit OFF overrides |
| Live menu smoke | Normal native menu inspected; native jobs, Stage F, completion mode 2, mailbox and two-context asynchronous batches verified; original save unchanged |

Freeze: `build-vulkan-embedded-default-v1`, renderer Vulkan, EXE SHA-256
`fb00eb546977c7442290d68313914fed439e4521acf8cd3db233f402d67fa4df`.
Its manifest identifies `embedded_runtime_profile=eu100-vulkan-native-v1`
and has an empty `runtime_defaults` map. The mutable normal Vulkan build
contains the same EXE. Earlier measured freezes are unchanged.

No new FPS gain is claimed for moving defaults. The previous matched
measurements explicitly selected the same approved runtime settings:
[29.57 FPS menu / 34.29 FPS round](native-geometry-summary.md).

Private evidence: `analysis/native-geometry/phase4/embedded-defaults-v1/`;
accepted constructor tests `profile-tests-v3`, runtime corpus `runtime-tests-v3`.
