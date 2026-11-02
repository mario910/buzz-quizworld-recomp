# Phase 3b: qualified default profile

| Gate | Result |
| --- | --- |
| Build/launch | Normal EU 1.00 Vulkan, AVX2, native FP/result guard, hot PPU and native title adapters select phase 3 and manifest `BUZZ_GEOMETRY_JOB=1`; both launchers preserve explicit `0` |
| Build identity | `build-vulkan-host-pipeline-default-v1`, EXE `2b0c34e6`; every byte matches measured `0c17df76` except the COFF link timestamp; all code/data sections and symbol addresses are identical |
| Corpus through normal launch | Default and explicit-off arms each 19,508 exact jobs / 58,195 transfers; zero differences/exclusions; exit 0; default executes 17,437 native jobs / 2,071 whole-job fallbacks |
| Live whole-job I/O | Same code/data: at least 1,275,000 completed comparisons PASS; concurrent-input skips excluded |
| Actual manifest-default frames | Native menu and round GPU frames inspected; normal geometry; both logs confirm mailbox and asynchronous two-context batches |
| Reversed menu FPS | 28.342 → 29.336, +3.510%; both pairs improve |
| Reversed menu CPU/frame | Workers: cycles −8.957%, CPU time −8.566%; process: cycles −4.920%, CPU time −4.899%; both pairs improve |
| Reversed round FPS | 32.760 → 33.694, +2.849%; both pairs improve |
| Reversed round CPU/frame | Workers: cycles −5.303%, CPU time −5.328%; process: cycles −3.287%, CPU time −3.588%; both pairs improve |
| Configuration validation | Native core test and real manifest guards pass; both launchers' default/override plans pass; PowerShell covers absent/empty/explicit values, restoration, 12 invalid manifests and 20 build selections |

FPS and CPU rows are the separate same-EXE reversed series in
[compact-profile results](native-geometry-phase3b-profile-results.md), bound
to measured EXE `0c17df76`; they are not new measurements of the relink.
The adoption build preserves the measured instructions and data exactly.

**Decision:** phase 3b's supported image-41 host pipeline passes and is
default-on for this qualified profile. Bare executables and unqualified
profiles retain their prior defaults. Unsupported jobs fall back as a whole.
Continue required phase 4 with shared host algorithms and real covered inputs.

Private evidence: `analysis/native-geometry/phase3/default-integration-v1/`;
prior immutable corpus/live/measurement bindings remain unchanged.
