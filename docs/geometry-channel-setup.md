# Native MFC argument setup

In native geometry job mode 1, ordinary channel wrappers directly assign the
five pure MFC argument registers: local-store address, effective-address high
and low words, transfer size and tag. The tag retains the SDK's five-bit mask;
the other arguments retain all 32 bits. Channel writes use the preferred word
of the original vector, as in the SDK.

The shortcut removes general channel classification and an MFC-engine lookup
from these assignments. It does not submit, combine, delay or reorder DMA.
Commands, atomic reservations, waits, tag updates, synchronization, events and
notifications still use the existing SDK path. The context's MFC engine may
be created at the first command/read instead of the first argument write;
argument writes do not access or change its state.

## Controls and audit boundaries

The experimental path defaults OFF. Set `BUZZ_GEOMETRY_CHANNEL_SETUP=1`
before loading/starting the game to enable it in job mode 1; `0` restores
general channel handling. Values other than `0` or `1` are rejected. Startup
logs print the effective choice. Modes 0 and 2 retain general handling.

An active recording or shadow replay always uses the original wrappers, even
when mode 1 has enabled the shortcut. Setting `SPU_CHHIST` before startup also
disables it, preserving the SDK's channel histogram. Environment controls are
startup choices, not live toggles.

The shared C++ path applies to Windows, Linux and Android native geometry;
there is no separate Gradle switch or SIMD implementation.

## Correctness qualification

`tests/geometry-channel-setup-test.cpp` compares the complete context against
the actual pinned SDK's `mfc_channel_write` for 5185 argument writes, including
full-width addresses, alignment boundaries, large sizes and reserved tag bits.
All 251 other channel numbers in 0..255 are rejected without context changes;
the reference MFC engine remains unchanged. Windows clang-cl, Linux x86 and
ARM64/QEMU comparisons pass. The fixture is in the offline CPU suite.

QEMU establishes instruction/ABI correctness only. PC throughput and Android
hardware performance require whole-game measurements.

## PC measurement

Two separate release cohorts ran with Windows locked, the same save/script,
1024x576 client, 1280x720 render extent, warmed shaders, mailbox presentation,
two draw contexts and unchanged native geometry/renderer/scheduler settings.
EXE/map identities and effective profiles were checked. No build, correctness
test, sampler or second game overlapped timed FPS/CPU windows. All ten
35-second Vulkan readbacks show the intended menu.

The first six-run cohort bracketed previous/new/new/previous with the same
immutable reference14. Previous and new heavy-window means were 44.2870 and
44.5970 FPS. However, reference14 itself changed from 37.6798 to 34.2522 FPS
between its two runs; this does not establish a reliable small FPS gain.

A second cohort used one frozen release EXE, explicitly OFF/ON/ON/OFF:

| Measurement | OFF | ON |
|---|---:|---:|
| Pooled heavy 45–60s FPS | 44.5821 | 44.5745 |
| Pooled 45–65s FPS | 46.3437 | 46.3539 |
| Process CPU ms/frame, 45–65s | 117.5627 | 115.7885 |
| SPURS pool CPU ms/frame, 45–65s | 51.7067 | 50.2629 |
| Process cycles/frame, millions | 431.3177 | 429.6940 |

This confirms throughput parity in this workload, with 1.51% lower process
CPU time/frame, 2.79% lower pool CPU time/frame and only 0.38% fewer process
cycles/frame. CPU time is aggregate work across threads, not frame latency or
power consumption. The small initial FPS increase was not reproduced by the
same-EXE switch. P99 remains roughly 28ms; the longer-window mean above 45
does not meet the stable 45 FPS goal. Actual round progression and Android
hardware performance remain unqualified. The option stays default-OFF.

Startup checks confirm the unset default and `SPU_CHHIST` suppress the
shortcut. The initial paired mode-2 startup failure reproduced in the prior
frozen build with argument setup disabled. It was subsequently traced to a
thin-triangle decision changed by approximate reciprocal projection. The
[strict projection policy](geometry-projection-audits.md) now repairs that
audit mismatch. Mode 2 continues to disable the MFC argument shortcut, so its
successful strict run does not provide additional coverage of that shortcut.
