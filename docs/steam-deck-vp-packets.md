# Steam Deck vertex-program upload batching, 2026-11-15

The upload batch passed full renderer-state comparisons but did not improve
net menu FPS. It remains a private experiment and was removed from the build.
Stable 30+ FPS at 1280x720 and 100% render scale is still unmet.

## Candidate and correctness

A separate 20.0387-second diagnostic counted 99,052,712 RSX method writes;
35,652,108 (35.99%) were vertex-program upload words at 0x0B80–0x0BFC.
The private batch covers those methods plus program load/start at 0x1E9C and
0x1EA0. It retains existing subchannel, packet type, mapping-prefix, engine,
and diagnostic gates, and the original path for unsupported packets.

The two renderer consumers have different upload cursors and limits. The
batch preserves the dispatcher's method-derived lane and 544-instruction
limit, and the legacy state's independent byte cursor and 1024-instruction
limit. Registers, seen counts, dirty flags, unsigned wrap, and partial mapped
prefixes are updated exactly as the original per-word handlers do.

ASan/UBSan fixtures reported 194,308 packet/state comparisons in each of the
native and checked modes, including 89,012 new VP packet cases. Eligibility
was checked across subchannels, types, alignments, counts and boundaries.
A separate 90-second in-game checked run reported 37,888 sampled complete-state
comparisons covering 851,806 words, with no mismatch. Its menu capture was
visually intact. These are correctness checks, not performance measurements.

An isolated 32-word incrementing packet took about 17.1–17.2 ns/word through
the original handlers and 2.9–3.0 ns/word through the batch. That excludes
FIFO processing and is not an FPS result.

## First menu series: compiler-profile mismatch

All timing runs used uncapped Vulkan, 1280x720, 100% scale, 512-draw batches,
two asynchronous draw contexts, zero pool spin, and `BUZZ_IMAGE36_SEARCH=1`.
Each ran 90 seconds with fresh private runtime/save/application shader-cache
state. Frames from 40–70 seconds were measured; CPU time used 45–65 seconds,
and the capture was at 80 seconds. No build, replay, profiler or other CPU
benchmark overlapped a timing series. The six arms ran sequentially in the
listed order; every measured frame had presented_delta=1.

A is the frozen image36-search reference. C is the candidate with
`BUZZ_VP_PACKETS=0`; B is the same candidate with `BUZZ_VP_PACKETS=1`.
The first candidate discarded old PGO data for two functions:
`gcm_rsx_process_fifo_unlocked` and `buzz_engine_state_packet`. Its enabled
versus disabled comparison alone would overstate the benefit versus the
existing build.

| Arm | FPS | p95 interval ms | Minimum 1 s FPS | CPU ms/presented frame |
|---|---:|---:|---:|---:|
| A1 | 28.533 | 41.182 | 25 | 153.467 |
| C1 | 28.333 | 40.659 | 26 | 156.071 |
| B1 | 28.400 | 40.784 | 25 | 155.167 |
| B2 | 28.667 | 40.454 | 27 | 155.277 |
| C2 | 27.833 | 41.730 | 23 | 156.358 |
| A2 | 28.500 | 40.607 | 24 | 155.627 |

Mean FPS: reference 28.517, candidate OFF 28.083, candidate ON 28.533.

The enabled batch recovered the candidate's loss, but matched the unchanged
reference. This did not establish a net optimization.

## Second menu series: fresh profiles

Only the changed GCM and draw-engine translation units were instrumented,
using atomic profile counters and explicit profile flushing at shutdown.
Training comprised a 100-second enabled menu, a 335-second enabled gameplay
run, and a 100-second disabled menu. The gameplay capture reached a question
round and rendered correctly. Training timings are not used as FPS evidence.
The two units then used their merged fresh profile; other units retained PGO6.
The resulting build reported no profile-hash mismatch. Its training
instrumentation and flush-only main definition were removed before timing.

The same six-arm protocol was repeated:

| Arm | FPS | p95 interval ms | Minimum 1 s FPS | CPU ms/presented frame |
|---|---:|---:|---:|---:|
| A1 | 28.567 | 41.158 | 24 | 155.693 |
| C1 | 28.433 | 41.118 | 24 | 156.287 |
| B1 | 28.700 | 40.863 | 25 | 155.406 |
| B2 | 28.600 | 41.756 | 25 | 155.500 |
| C2 | 28.567 | 40.835 | 24 | 155.353 |
| A2 | 28.800 | 40.613 | 24 | 153.553 |

Mean FPS: reference 28.683, candidate OFF 28.500, candidate ON 28.650.

With matching profiles, the enabled result is neutral versus the reference.
Its 0.15 FPS mean advantage over the disabled candidate is small, and p95
frame intervals did not consistently improve. This does not justify shipping
the additional code. No further round FPS qualification was performed.

Effective renderer profiles matched in both series. B2/C2/A2 captures from
both were inspected and showed intact menus. Fixed elapsed windows still do
not certify identical camera phases, and post-window screenshots cannot prove
every timed frame's content. Do not pool these results with older sessions or
infer a reveal improvement from them.

## Diagnostic memory attribution

The earlier 9,598-sample user-cycle capture lost eight chunks, and most hot
samples could not be unwound with DWARF. Matching the host libc build ID and
disassembling its frameless vector copy/fill routines allowed direct return
addresses to be recovered from sampled user stacks. All 988 samples in the
selected ranges had usable stacks: 767 copy and 221 fill samples. Their
weighted periods were 9.308% and 2.368% of the complete capture, respectively;
these selected ranges are not all possible memory routine variants.

The principal SPURS/channel return addresses were verified immediately after
calls to memcpy/memset. The largest callers were input staging and DMA copies.
Image-41 lazy scratch clearing and binary reuse are already present. The
measurements do not authorize dropping copies, notifications or ownership
rules. Separate grouped hardware counters were multiplexed about 66%; generic
cache misses were not classified as LLC or DRAM events.

## Reproduction and disposition

Local artifacts under `/home/deck/buzz/performance/optimization/`:

- `codex-vp-packets/`: private helper/integration scripts, SDK-oracle fixtures,
  logs, frozen binaries, raw/merged compiler profiles, both timing series,
  frame CSVs, CPU telemetry, renderer profiles and screenshots.
- `codex-hardware-counters/`: thread counters, method counts, matching-library
  mapping evidence, disassembly investigation and direct-return parser/results.

Executable SHA-256 values:

- Frozen reference and restored build:
  `591aa41c7d50bbccfb72f5a36632d3ff037e69128f0236fd3fe5a1d6971a39f8`.
- First candidate:
  `be13e24effbf4136082404f4961ff27a45046469d61ba6d29b0248c5c710ba9b`.
- Candidate using fresh renderer profiles:
  `52522c52aa02595262bfc849cc86492812377429f05ee64aaa9ea20c5b15f9cb`.
- Installed executable, unchanged:
  `189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.

Generated adapter files and private Ninja profile flags were restored. No raw
SPU lift, SDK source, installed executable or root optimization default changed.
Private captures, game-derived generated code and binaries are not committed.
