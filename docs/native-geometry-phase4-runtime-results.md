# Phase 4: image-40 runtime results

2026-11-02. Vulkan candidate `build-vulkan-animation-runtime-v3`, EXE
`e264d5123c3eedda999da9e4901fb53c16f9ffc4bf8a18ec52a2d1add56c8674`.
`BUZZ_ANIMATION_JOB=0/1/2`: retained lift / native / native retained with
original shadow I/O comparison. Bare EXE defaults to `0`; the qualified
EU100/Vulkan/AVX2 build defaults to `1`. Explicit `0` remains preserved.
Default freeze: `build-vulkan-animation-default-v1`, same measured EXE.

| Gate | Result |
| --- | --- |
| Image-40 corpus | Retained, native and native-retained comparison: each 17,714 exact jobs / 115,162 ordered transfers; all native jobs admitted, zero fallback or skipped comparisons |
| Image-41 regression | Native and checked: each 19,508 exact jobs / 58,195 transfers; existing 17,437 native / 2,071 fallback split preserved |
| Actual native I/O | At least 100,000 live native-retained menu comparisons; lift consumes actual native GET payloads and checks publications and job exit controls |
| Native frames | Menu and actual green solo round inspected; normal scene, characters and text |
| Runtime | Separate whole-job switch, bare EXE off; fixed TLS host workspace; unsupported job falls back before channel/output mutation; qualified Stage F fallback preserved |
| Renderer | Mailbox and two-context asynchronous batches verified in every completed run |
| Launch/default qualification | Normal launcher: 20 exact mixed image-40/41 jobs with the new default, repeated with explicit native OFF; parent environment restored. Nineteen invalid manifests rejected, twenty build selections checked; unavailable native owner compiles for baseline ISA and retains the reference |
| Native graphical launcher follow-up | Its manifest whitelist now accepts the qualified image-40 option; native tests and actual default/explicit-OFF launch plans pass. Game EXE and measured runtime are unchanged |

Four fresh same-EXE runs per series, `0/1/1/0`. Image-41 host jobs remain
enabled in both arms. FPS and CPU use separate series; all CPU values are
per completed frame. Menu window: 40–98 s; round: 200–270 s.

| Scene | Metric | Retained | Native | Change | Reversed pairs |
| --- | --- | ---: | ---: | ---: | --- |
| Menu | FPS | 28.972 | 29.567 | +2.052% | +3.666%, +0.458% |
| Menu | Worker cycles, millions/frame | 393.555 | 372.358 | −5.386% | −5.892%, −4.878% |
| Menu | Process cycles, millions/frame | 749.581 | 727.047 | −3.006% | −3.631%, −2.376% |
| Menu | Worker CPU ms/frame | 107.467 | 100.726 | −6.272% | −6.439%, −6.104% |
| Menu | Process CPU ms/frame | 203.732 | 196.486 | −3.557% | −3.741%, −3.372% |
| Round | FPS | 33.857 | 34.288 | +1.274% | +1.949%, +0.607% |
| Round | Worker cycles, millions/frame | 322.633 | 301.918 | −6.420% | −6.327%, −6.513% |
| Round | Process cycles, millions/frame | 571.147 | 551.321 | −3.471% | −3.406%, −3.536% |
| Round | Worker CPU ms/frame | 87.690 | 81.897 | −6.607% | −6.494%, −6.718% |
| Round | Process CPU ms/frame | 155.764 | 148.776 | −4.486% | −4.385%, −4.587% |

**Decision:** enable in the qualified build: both reversed FPS pairs improve
in both scenes, and all worker/process CPU comparisons decrease. Reduced
CPU work per frame is a separate benefit from FPS. Required supported
image-40 native work is complete; uncovered jobs retain whole-job fallback.

The first round CPU series was invalidated by a control-run `null-call`
before the measurement window. Its artifacts are retained; the entire fresh
`v2` CPU series passes. All eight accepted round measurement frames were
personally inspected. Camera/idle poses can vary between fresh runs.

Private evidence: `analysis/native-geometry/phase4/runtime-{build-v3,corpus-v5,live-v2}/`
and `runtime-{menu,round}-{fps,cpu}-v1/`; round CPU retry: `runtime-round-cpu-v2/`.
Mathematical/oracle checks and shared
code candidates: [phase-4 results](native-geometry-phase4-results.md).
