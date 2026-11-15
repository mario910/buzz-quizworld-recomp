# Command-thread copy attribution on Steam Deck

A targeted profile identifies small DMA reads as a candidate for further
investigation. It does not establish a speedup or a removable frame-time cost.
The preceding [vectorized index experiment](steam-deck-index-vector.md)
improved its isolated kernel but remained flat in-game.

## Capture validity

The unchanged qualified reference (`630007cb…`) ran a fresh 90-second menu
session at 1280×720 and 100% scale, with image-36 search enabled. Perf sampled
only thread `ch 701EB500`, using user cycles at 99 Hz and 8 KiB DWARF stacks
during seconds 45–65. Its 1,170 samples contain no lost events. Presentation
continued through 89.940 seconds, and the 80-second menu capture was inspected
and intact. This diagnostic run is excluded from clean FPS comparisons.

An earlier 199 Hz capture recorded 2,346 samples without loss, but presentation
stalled at 69.689 seconds after profiling ended. The harness closed the game
normally at 90 seconds, so exit code zero did not prove successful progression.
There is no final capture for that attempt. Its logs are retained; its stall
cause is unresolved. The successful lower-rate repeat supplies the numbers
below. Neither attempt changed game code.

## Callers and sizes

The largest individual libc instruction, offset `0x170bc5`, accounts for
9.09% of the sampled command thread's user cycles. Disassembly places it
immediately after the copy routine's first 32-byte source load. The nearest
exported symbol is not its semantic name. The sampled RDX register still holds
the original copy length at this instruction.

Of 98 samples at that instruction, period-weighted shares of the complete
thread profile by copy length are:

| Bytes | Share of thread user cycles |
| --- | ---: |
| 32 | 1.860% |
| 64 | 3.064% |
| 128 | 3.390% |

These are shares attributed at one sampled instruction, not complete copy
costs, call frequencies, byte-volume shares or proof of cache misses. Together
they account for 8.31% of thread samples. Call stacks attribute 8.61% at this
instruction to `spu_wrch` and 0.48% to job staging. Prominent lifted callers
include image39 `0x3470`, `0x3530`, `0x33F8`, image36 `0x0A40`, and the
`6874` job image. Unwinding need not reach the chain root to identify an
immediate caller; unresolved deeper callers remain unknown.

The unchanged list-transfer engine reads each LS descriptor, validates and
executes its transfer, then advances. This suggests testing bounded read
prefetch hints ahead of individual list GETs, while retaining all original
copies, list/stall behavior, validation, observers, fences and notifications.
No prefetch integration or FPS benefit is established by this profile.

## Evidence

Private artifacts are under
`/home/deck/buzz/performance/optimization/codex-chain-copies/`: both run logs,
frame CSVs, selected-thread identities, raw perf data, loss reports, call stacks,
sampled registers, disassembly, `analyze.py` and `copy99-entry-sizes.json`.
The parser checks all 1,170 samples and their summed period count against
perf's total before reporting copy-size attribution. The earlier failed parser
attempt split register output on blank lines incorrectly; its corrected results
are retained separately for the 199 Hz run and are not used above.

The subsequent [bounded prefetch experiment](steam-deck-dma-prefetch.md)
passed differential replay but has not established a net FPS improvement.
