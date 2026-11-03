# Documentation

Documents describe the current implementation, formats and development tools.
Completed phase trackers, session handoffs and benchmark reports are kept in
Git history rather than maintained alongside the reference documentation.

## Using the port

- [Preparation and game import](game-import.md)
- [Native PC launcher, display and textures](pc-launcher.md)
- [Input and offline mode](input-and-offline.md)
- [Phone buzzers](phone-buzzers.md) and [physical receivers](physical-buzzers.md)
- [EU original-disc profiles](disc-100-status.md) and [US profile](us-100-status.md)

## Runtime and development

- [Developer commands and repository layout](development.md)
- [Native jobs: geometry, animation and fallback](native-jobs.md)
- [Runtime defaults and overrides](runtime-defaults.md)
- [Renderer architecture and diagnostics](renderer.md)
- [Frame pacing](frame-pacing.md)
- [Profiling and validation](performance.md)
- [Source publication policy](publication.md)
- [Legacy Python launcher](legacy-python-launcher.md)

## Job and format references

- [Geometry data layouts](native-geometry-data.md)
- [Animation data layouts](native-animation-data.md)
- [Retained geometry kernels](native-geometry-kernels.md)
- [Input formats](native-geometry-input-audit.md)
- [Output conversion](native-geometry-conversion-audit.md)
- [Compressed indices](native-geometry-index-audit.md)
- [Transform and cull](native-geometry-transform-audit.md)
- [Output ring allocator](native-geometry-ring-audit.md)

The detailed SPU references describe the lifted code and retained diagnostic
implementations. Their register/frame requirements do not define the native
host pipeline's internal representation.

## Questions and custom packs

- [Building and installing packs](question-packs.md)
- [Question, text and media formats](question-format.md)
- [Knowledge Is Power import](question-import-kip.md)
- [Quiz TV import](question-import-quiztv.md)
