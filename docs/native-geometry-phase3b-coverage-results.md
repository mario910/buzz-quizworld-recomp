# Phase 3b: wider host-pipeline coverage

2026-11-02. `BUZZ_GEOMETRY_FALLBACK_REASONS=1` (diagnostic, default off, outside
the `BUZZ_GEOMETRY_JOB_*` family so measurement runners never set it) tallies
why recognized image-41 jobs take the whole lifted job; the replay prints it.

| Corpus fallback reason, before | Jobs | Change |
| --- | ---: | --- |
| Output storage 1 (SNORM16) unsupported | 1,408 | Host SNORM16 writer: SPU FMA `x*32767.5-0.5`, cflts 2^16, high halfword |
| then: vertex record stride not a multiple of 4 | 567 | Restriction removed; every writer stores at byte offsets |
| then: transitional conversion-cursor capacity | 223 | Not applied to the persistent path, which bounds its own publication |
| Out-of-palette index in the padding vertex | 584 | Retained: reading that LS quad at entry differed from the lift on 192 jobs |
| Additional blend streams / input storage 4 | 328 / 109 | Retained |

| Gate | Result |
| --- | --- |
| Corpus, native / checked / off | Each 19,508 exact jobs / 58,195 transfers; native 17,437 -> 18,487 (89.4% -> 94.8%); checked compares all 18,487 |
| Live JOB2 menu | >= 2,470,000 whole-job I/O comparisons PASS |
| Arithmetic | 3,145,728 values: host SNORM16 equals scalar SPU `fma`+`cflts` reference |
| Native round frames | Inspected; normal arena, host, Adam |

Same-session reversed series against `build-vulkan-embedded-default-v1`
(`fb00eb54`), each EXE with its embedded defaults; candidate
`build-vulkan-claude-coverage-v1`. Values per completed frame.

| Scene | Metric | Base | Candidate | Change | Pairs |
| --- | --- | ---: | ---: | ---: | --- |
| Menu | FPS | 40.59 | 40.70 | +0.25% | +0.34% / +0.17% |
| Menu | Worker cycles | 302.81M | 298.73M | -1.35% | -1.37% / -1.33% |
| Menu | Worker CPU ms | 81.62 | 80.35 | -1.55% | -1.34% / -1.76% |
| Menu | Process cycles | 587.56M | 585.00M | -0.44% | -0.64% / -0.23% |
| Round | FPS | 49.95 | 49.26 | -1.39% | -1.80% / -0.97% |
| Round | Worker cycles | 249.44M | 249.45M | +0.01% | +0.19% / -0.18% |
| Round | Process cycles | 449.81M | 447.89M | -0.43% | -0.20% / -0.65% |

The CPU series' contextual round FPS is 49.45 -> 49.54; the round FPS
difference is within scene variation. **Decision:** keep. Exact, with a
repeatable menu worker saving and neutral rounds. These runs were launched
outside the Codex sandbox; their absolute FPS is higher than earlier
documented series and must not be compared with them.
