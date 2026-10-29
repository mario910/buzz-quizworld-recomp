# Source publication scope and checks

## What is being published

- Port repository: the explicitly selected `main` branch only.
- SDK: a **new source-only root** on `buzz-quizworld`, preserving upstream
  source/attributions and our runtime changes, not the old commit ancestry.
- zlib: its existing separately licensed upstream submodule.
- No native game executable, generated translations, asset package, captured
  jobs, firmware, personal paths configuration or private working notes.

The port has private local branches and ignored research output. A normal
explicit-ref push does not publish them. **Do not use `git push --all`,
`--mirror`, or push all tags.** A file ignored today can still exist in an
ancestor, which is why the checks inspect reachable history as well as HEAD.

## Important SDK fork caveat

The upstream history contains Sony firmware PRXs and translated firmware
under `fw_spu/`. Upstream commit `0952e1c0` removed them from the tip, **not
from history**. Merely publishing a later ordinary commit would retain them.

The `buzz-quizworld` publication branch therefore starts a new root from the
audited source snapshot, retaining licenses and source provenance in
`ps3recomp/BUZZ_FORK.md`. Copied wiki PDF/text references and old diagnostic
logs are omitted from that snapshot; the online reference remains linked.
Synthetic upstream instruction/lifter test fixtures remain source tests,
not retail game/firmware captures.

**At the repository owner's explicit choice, the existing GitHub fork and its
other refs (including `master`) are retained.** This publication does not
purge their inherited history or remove previously hosted firmware objects
from GitHub's fork network. Only the new branch's reachable content is
audited as our source export. Do not describe the entire fork as purged or
legally cleared. A separate non-fork repository would provide clearer
isolation; hosting/history removal is a separate task.

The submodule specifies shallow cloning, and README uses
`--shallow-submodules`, to avoid fetching the fork's full old history in a
normal port checkout. This is a convenience, not an access restriction or a
remote-history cleanup.

## Reproducible pre-push checks

After committing the exact source changes intended for publication:

```powershell
.\toolenv\Scripts\python.exe tools/audit-publication.py --repo . --ref main --profile port
.\toolenv\Scripts\python.exe tools/audit-publication.py --repo ps3recomp --ref buzz-quizworld --profile sdk
git diff --check
git -C ps3recomp diff --check
git status --short
git -C ps3recomp status --short
```

Run the history audit from the full local working repository, not a shallow
checkout. It rejects known asset/output/private-config paths, common binary
formats, unexpected binary blobs, large blobs and common credential patterns.
It scans **all reachable blob contents** and historical changed paths for the
selected ref, including files deleted at the tip. It checks submodules only
when invoked separately. It does not inspect unpushed refs, GitHub caches,
issues, releases or the fork's other branches. A clean result is a heuristic
check, **not** an exhaustive secret scanner, provenance proof or legal opinion.

Review file lists and source provenance manually too. Test the README flow in
a fresh source-only checkout with locally supplied inputs before announcing
build reproducibility. Do not create binary release assets from the game build.

Push the clean SDK branch first, then commit/push the port's gitlink pin.
Use only the named refs after confirming the remotes belong to the owner:

```powershell
git -C ps3recomp push origin buzz-quizworld:refs/heads/buzz-quizworld
git push origin main:refs/heads/main
```

No force push or deletion of existing remote refs is part of this procedure.
If a remote branch already has unexpected content, inspect it before proceeding.

## Legal limitations and attribution

The code audit reduces publication risk; it cannot decide copyright,
anti-circumvention or trademark questions for every jurisdiction. In the EU,
the software decompilation/interoperability exception has conditions and
limits; it is not general permission to redistribute game code.
[Directive 2009/24/EC, Article 6](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32009L0024).

The source license does not cover locally translated game/firmware code.
LGPL components also impose separate requirements for distributing linked
executables; see [LGPL 2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html)
and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). No binary game release
is being made. Obtain qualified legal advice if you need assurance beyond
this technical content/licensing review.

## Validation record — 2026-10-29

- Clean SDK export: `bc25dcc5515e3752292688061772915279ffea22`, one root commit,
  883 reachable source blobs checked; no findings from the publication guard.
- Port code through `f5a56bb`: all 254 reachable blobs checked, no findings.
  Documentation-only follow-ups are re-audited before push.
- A separate source checkout with the clean SDK and zlib regenerated all lifts
  and completed the full `Build-Buzz.ps1 -Jobs 6` pipeline successfully. It did
  **not** reuse generated files or compiler objects from the developer build.
  It reused the installed Python/tool environment (Python 3.13; `pip check`
  passed), so this is not a claim of a fresh pip/network-install test.
- The clean-build EXE's local SHA-256 was
  `BD721130B2A9B619872AC2AB87BB55A600BB5A3D23254DCDF2351B044282DD37`.
  That EXE remains local/ignored and is **not** a release asset.
- Synthetic publication tests: 3 passed. Launcher/model/native-parser/adapter
  and withdrawn-UI suite: 17 passed. Buzzer/protocol/router, frontend/boot-hook
  and command-line launcher fixtures passed. Image36 generator and present
  phase-report regression tests passed.
- PowerShell entry-point syntax checked; the generation bound regression
  passed on Windows PowerShell 5.1 and PowerShell 7. The clean build found and
  fixed the old Double-to-hex bound error; the boot-hook fixture also gained
  the missing synthetic clock stub. No gameplay/runtime performance settings
  were changed by these publication fixes.
- No new full gameplay, physical-controller or FPS claim is made by this
  publication check. Existing status observations in README are qualified.
- After publication, a recursive `--shallow-submodules` clone from the actual
  GitHub port URL succeeded. Both pinned SDK/zlib commits were fetched and
  checked out, the worktree was clean, and the SDK HEAD had exactly one
  reachable commit with no `fw_spu`/firmware binary paths in that ancestry.
