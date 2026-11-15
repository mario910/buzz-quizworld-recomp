# Steam Deck texture retirement investigation

Stable 30+ FPS remains unverified. This investigation follows the combined
CPU/vertex and presentation tests recorded in
[the packed vertex experiment](steam-deck-packed-vertices.md).

## Synchronous submission attribution

Private diagnostic variants add source-call-site spans around synchronous
Vulkan submissions and full batch drains. Source snapshots and line maps
are retained with each binary; line numbers are not interchangeable between
variants. Spans use the existing renderer trace clock and capture window.

The initial diagnostic menu trace contains 81 complete FIFO intervals,
with zero dropped records. Each interval has a draw drain from `vk_begin`
(2.980 ms including 2.882 ms of nested fence waits), followed by a synchronous
`vk_eng_upload_region_z` submission (0.366 ms including 0.292 ms of fence waits).
These are diagnostic CPU-observed durations, not GPU busy time or clean FPS.

Enabling the existing immutable upload batches removes those two calls but
leaves one full drain per interval. Additional caller tracing attributes it
to `vk_eng_release`: 82 calls across 82 complete intervals, 3.104 ms/interval
including 3.002 ms of nested fence waits. Both captures have zero dropped
records. Upload batching therefore mostly moves this wait to resource release;
the wait durations are not additive savings.

## Deferred retirement prototype

The private prototype removes an eligible ordinary texture from the live
handle table immediately, retaining ownership in the current recording
context. Eligibility requires asynchronous batching, a healthy active recording,
an ordinary recyclable texture with complete image/view/memory ownership, and
the existing texture pool. Views, render targets, volume textures and snapshots
retain the original release path. Full lists and oversized allocations also
fall back to the original synchronous release.

Each of four possible contexts holds at most eight pending textures; their
combined actual allocation size is bounded by 64 MiB, in addition to the
existing pool's separate 64 MiB limit. Collection occurs only after successful
context-fence completion, synchronous submission completion for that context,
or a full device-idle completion. The current recording follows every use
issued before release. A submission fence covers earlier submissions on the
same queue, as specified by
[Vulkan synchronization scope](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).
Actual destruction still requires completed GPU references to the
[image](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyImage.html)
and [view](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyImageView.html).

Completed objects enter the existing pool only if insertion requires no
eviction; otherwise they are destroyed directly. This avoids recursively
draining a new batch from a fence-completion callback. Destruction still uses
the descriptor-cache image-view invalidation wrapper. Pending textures are
never available to pool lookup. Readbacks and guest completion retain their
existing drains.

## Validation and private artifacts

Actual RADV GPU fixtures passed all twelve combinations of retirement off/on,
upload batching off/on, and batch sizes 1/3/512. They retain the existing draw,
descriptor, constant, render-to-texture, depth, MRT and resource-lifetime checks.
Additional pixel checks exercise twelve differently colored retired textures,
context rotation, the eight-object fallback, the 64 MiB fallback using real
2048-square allocations, and shutdown with a pending texture. Batch size one
submits immediately and intentionally exercises the original release fallback.
Both pending ownership and allocation bytes are empty after completed drains.

A completed 90-second menu smoke test has an intact captured frame. GPU
fixtures do not constitute new SPU replay evidence; the build provenance
limitation below also applies to the diagnostic variants.

## Clean menu comparison

Six fresh-save/cache runs used X11, 1280×720, 100% scale, uncapped mailbox,
512-draw batches, two contexts, zero worker spin and image-36 search enabled.
Timing covers seconds 40–70, CPU work 45–65, and capture 80 of each 90-second
run. All six captures were intact and effective renderer settings matched.
No builds, audits or profilers overlapped the timing runs.

A is the qualified reference with upload batching disabled. C is the prototype
binary with upload batching enabled and deferred release disabled. B is the
same binary with both enabled. B/C isolates deferred release. A additionally
differs in compiler/profile, upload path and the geometry objects described below.

The final restoration hash check found that copying the original geometry
headers back with preserved older timestamps had left bounded-zero object
files from the earlier combined trial in the incremental build. The prototype
and diagnostic binaries therefore also contain that earlier geometry change,
despite restored header contents. Source-file equality alone did not establish
binary equality. This limits attribution against A; it does not invalidate
the same-binary B/C comparison or the renderer-only GPU fixtures. Archived
objects and timestamp/hash provenance preserve the discrepancy. No new
whole-job replay qualification is claimed for these binaries. Restoring a
header now explicitly invalidates its dependent objects before verification.

| Arm | FPS | Median ms | p95 ms | Aggregate CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.367 | 34.785 | 42.985 | 155.213 |
| C1 | 28.200 | 34.909 | 43.220 | 155.065 |
| B1 | 28.433 | 35.093 | 42.023 | 154.154 |
| B2 | 28.567 | 34.633 | 41.872 | 154.899 |
| C2 | 28.333 | 34.945 | 42.172 | 154.502 |
| A2 | 27.933 | 35.156 | 44.839 | 156.142 |

Means were A 28.150, C 28.267 and B 28.500 FPS. This is a small positive lead,
not a stable-30 result: enabled runs had one-second bins as low as 23 FPS and
p95 intervals above 41 ms. The closest enabled/reference pair differs by
only 0.067 FPS, and the reference runs drifted by 0.433 FPS. Four changed
Vulkan functions discarded their old PGO counts. No refreshed-profile or
question/reveal FPS qualification followed, so the prototype remains archived
without changing production defaults.
Of the 1,710 measured enabled intervals, 1,115 (65.2%) exceeded 33.34 ms.

The frozen candidate SHA-256 is
`263f4885065bd864f3a3f8fc2fe444705d3602ffaf30b48fe9edcb5721869d8a`;
the reference is
`630007cbc06f670ccef5d21cec7e41b8cf4ba148e1a48f8ed6f5cfa01fd0508d`.

A follow-up trace using the maintained call-site instrumentation recorded 83
complete intervals with no synchronous submissions or full batch-drain calls.
Fence waits nested in FIFO processing totaled 6.712 ms/interval. The capture
dropped no records and its final menu frame was intact. This confirms the
intended synchronization change, not a corresponding FPS gain: draw-context
reuse and separate presentation waits remain, and parallel producer work is
not shortened. The diagnostic binary is
`e0bdd09b5c11a08c98af7128a8412771c7d99421dee23ce5045d95e63b0dcd7a`.
The same diagnostic binary with deferred retirement disabled recorded 82
release drains in 82 complete intervals (3.249 ms/interval), with the nested
synchronous submissions correctly attributed to their generated source lines.
Its capture also dropped no records and ended in an intact menu. Neither
diagnostic run contributes to the clean FPS comparison above.

Private evidence is under `/home/deck/buzz/performance/optimization/`:
`codex-vulkan-sync-calls/` retains the initial attribution variants and traces;
`codex-texture-retire/` retains the prototype header, integration script,
generated source snapshot, expanded GPU fixture, hashes and game runs. No game
assets, generated game code, captures or binaries belong in Git.

After invalidating the restored geometry-header dependencies, the prepared
executable rebuilt byte-identical to reference `630007cb…`. The SDK remains
clean at `79efc280dad27271d5f68077acb0247df1e88dc7`; the installed executable
remains `189814d1e5ad5372fbf16e7e302cc79c6e969b911a0c1d7089d413df833d7d50`.
Only diagnostic attribution, its parser tests and these findings are retained
in the repository. The retirement prototype is not enabled or shipped.
