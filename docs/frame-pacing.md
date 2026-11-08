# Frame pacing

The frame limit is a live host setting: **30 FPS, 60 FPS or Uncapped**
(`fps_limit=30`, `60` or `0`). It is independent of Vulkan present mode.
Mailbox prevents a FIFO backlog but does not itself impose a frame cap.
Select it in General > Frame limit or the native launcher, then Save settings
to retain it. Existing 30/uncapped files retain their previous meaning.

`port/frame_limit.cpp` places its pacer at the real `cellGcmResetFlipStatus`
import (NID `0xB2E761D4`). Buzz calls it once after its frame wait, before
starting the next frame. Registration uses the import NID rather than a
release-specific executable patch.

Each submitting thread owns a rolling 1/30- or 1/60-second deadline. If late, the
next deadline still advances by one period; it is not reset to `now+period`
on every miss. Whole-period overruns, cap changes and host-menu pauses discard
stale debt rather than causing a catch-up burst. Windows waits use a
high-resolution timer with a bounded spin tail.
Switching between 30, 60 and uncapped resets the schedule, so a stale deadline
cannot keep the old cap in effect. The extra Vulkan presentation-smoothing
grid remains specific to 30 FPS; 60 uses the existing low-latency worker path.
This changes only the host cap, not game clocks, audio rate or interpolation.
It cannot guarantee 60 FPS or remove game-side frame-rate limits.

The original reset implementation still runs. RSX/FIFO work, vblank and audio
continue during the host wait. When capped, an executed flip can signal its
completion immediately after presentation instead of adding a second wait
for synthetic vblank. Unsubmitted frames and rendering fences are not bypassed.

Do not sleep on the RSX present worker: that worker also writes guest labels,
so holding it up delays rendering fences. The presentation paths must consume
the pending-flip flag exactly once; counting both the post-drain flip and its
still-pending flag would produce duplicate presentations and inflated FPS.
Paused host-menu redisplays are counted separately from completed game frames.

Shared settings and the in-game menu are documented in
[the native launcher](pc-launcher.md). The runtime reads the host settings,
not the SDK template's reference `config.toml`.

`tests/scripts/test-frame-limit.ps1` checks pacing behavior;
`perf/measure-frame-limit.ps1 -LiveSwitches` exercises live cap changes.
Use [the measurement rules](performance.md) when comparing capped and uncapped
behavior. No fixed FPS guarantee follows from the existence of the cap.
