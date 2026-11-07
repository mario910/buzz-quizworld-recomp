# Native image39 descriptor requests

`BUZZ_IMAGE39_NATIVE_REQUEST` is an experimental, default-OFF integer request
driver for the pinned EU 1.00 C0BC image. It admits reviewed callers before
mutating state, prepares cache/output publications in host code and issues a
real MFC GET on a miss. Unsupported contexts retain the entire original
request. This is independent of the runtime floating-point selector:
`SPURS_NATIVE_IMAGE39_FP=0` still permits an enabled integer request driver.

## Search contract

The driver matches all four packed words of each used record and retains the
last matching ordinal in every lane. Replacement preserves signed age
comparisons, the first-oldest tie rule, room selection and packed auxiliary
lanes. The current implementation retains its original combined search and
age reduction.

Cache/output records, the complete packed cache-result slot, live r3,
nonvolatile registers, return controls and DMA arguments retain their original
values. The enclosing native request still omits dead register/frame work;
it does not reproduce every intermediate SPU register. Tag completion,
coherency, notification and actual GET payload handling remain in the runtime.
There is no persistent host cache of guest pointers or previous admission.

## Selection

- CMake: `-DBUZZ_IMAGE39_NATIVE_REQUEST=ON`, with the regenerated reviewed
  image39 source. `generate-image39-native.py` and, when selected,
  `generate-image39-kernels.py` install the checked request hook.
- Windows build helper: `-NativeImage39 -NativeImage39Request`. Omitting the
  request switch resets its CMake option OFF.
- Android: `-PbuzzNativeImage39Request=true` with `buzzNeonJobFp`; the property
  defaults false and reference29 keeps it OFF.
- Runtime: `BUZZ_IMAGE39_REQUEST=0` retains the original request in an enabled
  build; `1` selects the native request. `BUZZ_IMAGE39_REQUEST_STATS=1` enables
  diagnostic attempted/native counters and must be kept out of timing cohorts.

Other renderer, geometry and SPURS experiments are separate options. Enabling
this request alone does not reproduce a complete measured experimental bundle.

## Correctness evidence

The oracle generator verifies the original C0BC source hash. On Windows
clang-cl, Linux x86 and ARM64/QEMU, the request test passes 707 captured cases
(703 admitted: 440 hits and 263 misses), 928 admitted synthetic cases, 96
synthetic fallbacks and 21 unsupported-context mutations. The captured original
LS/register output is checked independently before native live-state, published
bytes and ordered GET comparisons.

Another 54 cases cover 1, 7, 8, 31, 32, 255, 256, 4095 and 4096 used entries,
duplicate keys, signed age wrap/ties, packed key/stride/tag fields and exact
128-bit cache-result slots. Unused records deliberately match the requested
key, so a rounded-up SIMD tail would produce a visible test failure. QEMU is
a correctness check, not a phone performance measurement.

The offline CPU runner accepts `--image39-request-corpus` and
`--image39-source` to include this independent oracle. Captured game source and
data remain private.

## Performance decisions

A bound diagnostic counter read confirms that the existing native request
handles 9747369 of 9910325 attempted requests (98.3557%) in the maintained PC
workload. This confirms active admission, not a speedup. A startup mode of 1
alone would not establish how often the path actually runs.

An experiment separated matching from replacement ages and searched two
records at a time with AVX2 or four with ARM64 NEON. It passed the request
oracles and exact packed result-slot tests, including poisoned partial tails.
Six 72-second runs compare it with the frozen maintained cache/host-command
build and the same immutable reference14, under the same locked-session,
window, seed and effective-profile guards. Both nonreference builds use
FP mode 0, native requests ON, four-way index caching and host commands ON.
No build, correctness test, sampling profiler or second game overlaps timing.

In this cohort, pooled heavy-window (45-60s) FPS is 41.2826 for the maintained
control and 41.1775 for the experiment. Process CPU time is 125.7785 versus
126.3595 ms/frame over separate 45-65s counter windows. Neither metric
establishes an improvement, so the experiment is **not retained**. The current
native request and its default-OFF selection remain unchanged; the stronger
oracle fixtures are retained.

The control EXE is
`875017011972f68eea90d0e68d04b3f89f3b862f3aec84af0144a35b169840cf`;
the rejected EXE is
`3686150ceab775bf01b91e607c6c2735035f583822caf12434b24b6fd91cf91b`.
Private `request-search-*` artifacts preserve the raw frames, CPU counters,
readbacks and bindings. This decision does not qualify stable 45 FPS,
question/answer/scoring progression or Android hardware performance.
