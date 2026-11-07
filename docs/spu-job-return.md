# Native completion of SPURS jobs

`BUZZ_SPU_JOB_RETURN` is an experimental, default-OFF build option. It handles
an already completed SPURS job's return to the SDK's existing `0x3FF00`
job-manager sentinel on the native execution driver's stack. No guest work,
DMA, output publication, fence or completion signal is omitted.

The original resolver eventually recognizes that sentinel and calls
`spu_halt`, which jumps back to `spu_run_with_halt` using `longjmp`. The
adapter keeps IRQ restoration, diagnostics and task/overlay residency before
that decision, then recognizes completion before unused lifted-code lookup.
For admitted direct driver callbacks, it ends the drain normally and returns
the same `halted=1` result. Other paths retain the original non-local halt.

## Admission and nesting

The marker belongs to one live driver frame and context. The actual drain
callback must be `spu_indirect_branch`; the resolver consumes that marker at
entry. A resolver called by another guest callback cannot inherit it. Nested
drivers restore the enclosing frame's marker on exit. Frame state is volatile
across the existing `setjmp`/`longjmp` boundaries.

The path requires depth zero, owned local store, the sentinel link register,
no pending trampoline, IRQ frame, saved IRQ or enabled interrupts. Lockstep
and instruction observers retain the old halt. Policy modules retain their
existing dispatcher. Registered code at the sentinel is still never executed
for a completed ordinary job, as in the original resolver.

On Windows x64 clang-cl, the adapter explicitly restores MXCSR and the x87
control word from the real saved jump buffer before normal driver cleanup.
This preserves the Windows `longjmp` floating-point behavior without its
stack unwind. Other Windows jump-buffer ABIs retain the existing halt path.
Linux x86 and ARM64 use the existing POSIX floating-point behavior.

## Build and rollback

- CMake: `-DBUZZ_SPU_JOB_RETURN=ON`; bare builds default OFF.
- Windows helper: `-SpuJobReturn`. Omission explicitly resets the cached
  option OFF; it cannot be combined with `-ReferenceOnly`.
- Android: `-PbuzzSpuJobReturn=true`; the property defaults false and
  reference29 explicitly keeps it OFF.
- An enabled build defaults to the new route. Set `BUZZ_SPU_JOB_RETURN=0`
  before starting/loading it for same-binary rollback; `1` enables it.
  Malformed values are rejected. Startup logs print the effective choice.

The generated adapter composes with `BUZZ_SPURS_EXECUTION_TRACE`. The pinned
SDK files are not edited. CMake checks its source anchors and generates the
replacement channel translation unit in the build directory.

## Verification

`tests/scripts/test-spu-job-return.py` compiles the real generated channel
runtime with the pinned SDK dispatcher, interpreter, job and coherency code.
It compares OFF/ON results in 34 modes on Windows clang-cl, Linux x86 and
ARM64/QEMU. Cases include normal and direct callbacks, indirect callbacks
with live C continuations, nested jobs, lifted-call depth, IRQ restoration,
resident overlays/code, policy modules, foreign LS, registered targets,
instruction observers, floating-point state and existing interpreter,
stop/halt and step-cap regressions.

For example, after configuring an enabled prepared build:

```sh
python3 tests/scripts/test-spu-job-return.py --channels-source build/buzz_spu_job_return.c
python3 tests/scripts/test-spu-job-return.py --arm64 --channels-source build/buzz_spu_job_return.c
```

Windows uses a Visual Studio developer shell with `CC=clang-cl`. ARM64/QEMU
checks correctness only; Android hardware performance is not established.
This option remains an experiment pending broader gameplay qualification.

A 72-second locked-session strict geometry run with native job completion ON
reported at least 1,510,000 successful whole-boundary/I/O comparisons and no
differences. A separate read-only live snapshot recorded 1,439,220 checked,
8 skipped and 0 failed comparisons. Strict mode 2 retains reference projection
and padding; this check does not validate the relaxed mode-1 reciprocal policy.
An execution-trace-enabled Windows build and its 34 OFF/ON runtime modes also
pass. Stronger CMake anchor validation produces byte-identical ordinary
generated channels to the measured final release.

A separate 600-second locked-session functional run used the final EXE,
ordinary test-mailbox buzzer input, two distinct saved players and native job
completion ON. Vulkan readbacks confirmed category selection, successive
rendered questions, both players' selected answers and scores increasing from
0 to 250 each, followed by another question. This qualifies a basic
question/answer/scoring sequence, not an entire match. It used the supported
30 FPS cap and continuous diagnostic readback; its frame timings are excluded
from performance comparisons. No Windows unlock or OS key injection was used.

## Measured PC result

Six 72-second runs used one locked Windows session, the same save/input script,
warmed shader cache, a 1024x576 client and 1280x720 rendering. Their order was
reference14, option OFF, ON, ON, OFF, reference14. OFF/ON used the same EXE
(`2974056c77faadfef4bf23df187a46693bd65467aa302697fa5a7845202d35f3`),
including explicit Windows floating-point restoration. Reference14 remained
`d66601b4c612eb4006597d285e9d247ed538692d269cadb1448219179fb903fb`.
No builds, correctness tests, sampling or second game overlapped timed windows.

| Metric | Same EXE OFF | Same EXE ON |
|---|---:|---:|
| Animated menu, fixed heavier 45–60 s interval, pooled FPS | 44.226 | 46.447 |
| Animated menu, 45–65 s interval, pooled FPS | 45.998 | 48.264 |
| P99 presentation interval, 45–65 s, two repeats (ms) | 28.193 / 28.605 | 27.324 / 27.303 |
| Intervals above 22.22 ms, 45–65 s, two repeats | 42.58% / 43.62% | 28.57% / 27.25% |
| Process CPU time per presented frame, pooled (ms) | 119.209 | 111.972 |
| SPURS pool CPU time per presented frame, pooled (ms) | 52.182 | 48.364 |

The incremental heavier-interval gain is 5.02%; process CPU/frame falls 6.07%
and pool CPU/frame 7.32%. Aggregate process cycles/frame fall 4.49%. Reference14
produced 38.004 / 37.559 FPS in that same heavier interval (pooled 37.782).
These are repeated menu results, not stable 45 FPS, gameplay/scoring validation,
Android measurements or a power measurement. The final candidate still has
substantial intervals over the 22.22 ms budget. Vulkan readbacks confirmed the
same menu and no visible rendering regression in the six captured frames.

An earlier prototype's interrupted cohort is excluded: a new indirect-callback
test found that the first completion guard could resume a live C continuation.
The driver ownership marker above fixes that case. A second complete prototype
cohort preceded the Windows FP restoration and does not replace these final
measurements. Private EXEs, raw profiles and readbacks are not tracked in Git.
