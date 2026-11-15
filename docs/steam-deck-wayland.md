# Native Wayland surface formats on Steam Deck

The pinned Vulkan backend could not start a native Wayland window on the
measured Deck. Its fixed 64-entry surface-format array truncated the list,
and `VK_CHECK` treated `VK_INCOMPLETE` as a fatal error. The resulting
headless fallback then failed the port's requirement for a Vulkan window.
An independent `vulkaninfo` query on this Deck reports 81 Wayland formats
and two X11 formats, corroborating the fixed-capacity failure.

The port-owned Vulkan adapter now queries the required count, allocates the
complete list, and retries enumeration if the list grows between queries.
Retries are bounded at eight. Allocation and driver errors propagate, every
allocation is freed, and failed enumeration leaves the output unchanged.
Selection retains the existing BGRA UNORM, then RGBA UNORM, then first-entry
preference, including the chosen entry's color space and legacy single
undefined-format handling. It does not select from a truncated list.

This follows Vulkan's [surface-format enumeration contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceFormatsKHR.html).
The SDK pin is unchanged; the guarded adapter replaces only the old format
query/selection block. The helper runs at swapchain creation, not per frame.

## Validation

With Vulkan development headers and a C compiler installed:

```sh
python3 tests/scripts/test-vulkan-surface-formats.py --cc clang --sanitize
```

The display-independent mock test passes 66,574 cases with AddressSanitizer
and UndefinedBehaviorSanitizer. It covers every preferred-format position
in lists of 1–257 entries, preference order and color-space pairing, growing
and shrinking lists, empty results, driver errors, allocation failures
including a retry and the largest representable count, and bounded retry
exhaustion. The size-overflow branch is conditional on address width; this
Deck run exercises the 64-bit allocation-failure path.
It checks cleanup and unchanged output on failure. No Vulkan loader, game
assets or live display are required for this test.

The Linux game build succeeds. Its generated Ninja file is byte-identical
to the preceding qualified configuration, preserving all per-file PGO6,
floating-point, aliasing and linker flags. The changed `vk_create_swapchain`
control flow produces one expected PGO hash mismatch (66 old counts discarded);
there are no other profile mismatches. No new PGO training is claimed.

The formerly failing native Wayland launch now creates a 1280×720, four-image
BGRA UNORM swapchain with mailbox presentation. A 90-second scripted run
reaches an intact rendered heavy menu. Its preliminary 40–70-second window
averages 29.0 FPS with a 40.310 ms p95 frame interval; this single run does
not establish an improvement or meet the stable 30+ FPS goal.

## Alternating menu comparison

A is the previous qualified executable on X11, C the fixed executable on
X11, and B the same fixed executable on Wayland. All enable image-36 search.
Six private 90-second runs used FPS windows 40–70 seconds and CPU windows
45–65 seconds, with fresh saves, internal scripted buzzer input and separate
shader caches. The order and complete sampled-window statistics are below.

| Arm | FPS | p95 frame ms | Lowest one-second FPS | CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.700 | 40.094 | 26 | 154.761 |
| C1 | 27.700 | 43.729 | 23 | 157.744 |
| B1 | 27.667 | 43.942 | 23 | 157.732 |
| B2 | 27.933 | 43.885 | 23 | 157.097 |
| C2 | 27.800 | 42.262 | 24 | 157.853 |
| A2 | 27.800 | 43.435 | 23 | 157.419 |

Means: previous executable on X11 28.250, fixed executable on X11 27.750,
and fixed executable on Wayland 27.800 FPS. The previous executable's first
run is faster than its last (28.7 versus 27.8); its last run matches the last
fixed X11 run. This series shows no Wayland performance advantage and does
not establish exact performance equivalence between executables. The raw
means must not be presented as an improvement or silently adjusted for drift.

Effective profiles match: 1280×720, 100% scale, mailbox, uncapped,
512-draw batches, two contexts and zero worker spin. All sampled presentation
deltas are one. B2, C2 and A2 captures show intact menus. No builds, CPU tests
or profilers overlapped these measurements. Matching elapsed windows cannot
guarantee identical animated camera phases. No question/reveal timing claim
or stable 30 FPS qualification follows from this menu-only experiment.

The source change is retained as a startup correctness repair. It does not
force a different SDL driver or change rendering settings. The installed
executable remains untouched and the performance goal remains open.

## Private evidence

Private build logs, test output, the pre-fix failing launch, frozen executable,
run manifests, frame timings and captures are retained under
`/home/deck/buzz/performance/optimization/codex-wayland/`.
No generated game code, captures or binaries are committed.

- Previous qualified executable: `591aa41c7d50bbccfb72f5a36632d3ff037e69128f0236fd3fe5a1d6971a39f8`.
- Fixed executable: `630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.
- SDK pin: `79efc280dad27271d5f68077acb0247df1e88dc7`.
- Installed executable, unchanged: `189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
