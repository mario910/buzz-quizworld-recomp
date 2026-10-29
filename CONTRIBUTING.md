# Contributing

Start with the exact game/ELF requirements in README. Work from the pinned SDK
submodule; its upstream default branch is not the tested runtime for this port.

- Keep game assets, firmware, keys, generated/lifted code, captures, job
  corpora, saves, logs and binaries out of commits and public attachments.
- Change generators or hand-written adapters, not `port/recompiled` or
  `port/spu`. Regenerate locally and keep hash guards; do not bypass them to
  accept another release of the game.
- Preserve upstream copyright/license notices. New contributions to the port
  are submitted under the root MIT license unless explicitly agreed otherwise.
- Run relevant synthetic tests. Check native kernels against the lifted
  reference locally before claiming correctness; do not commit the corpus.
- Measure FPS with the same build, settings, scene and warm-up. Report frame
  times and variance, not just a single peak FPS. Do not silently disable audio,
  video, effects, guest jobs or synchronization to improve benchmarks.
- Background automation must use the game's internal scripted buzzer input,
  not desktop key injection or focus stealing. Never kill somebody else's
  running instance. See [developer tools](docs/development.md).

Before publication, run the checks in [docs/publication.md](docs/publication.md).
They are safety checks, not a legal opinion or a complete secret scanner.
