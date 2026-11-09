# Source publication scope and checks

## What is being published

- Port repository: the explicitly selected `main` branch only.
- SDK: the normal fork `master`, retaining upstream ancestry and the original
  Buzz development commits. Runtime/codegen are pinned to the validated
  v0.17.0 code, not upgraded to v0.18.0.
- zlib: its existing separately licensed upstream submodule.
- No native game executable, generated translations, asset package, captured
  jobs, firmware, personal paths configuration or private working notes.
- Windows preparation releases contain the compiled BuzzPrepare wizard, its
  support libraries, matching Qt source, and the port and pinned submodule
  source files used to build the game locally. See the
  [Windows packaging workflow](../preparation/README.md#windows-release-package).

The port has private local branches and ignored research output. A normal
explicit-ref push does not publish them. **Do not use `git push --all`,
`--mirror`, or push all tags.** A file ignored today can still exist in an
ancestor, which is why the checks inspect reachable history as well as HEAD.

## Important SDK fork caveat

The upstream history contains Sony firmware PRXs and translated firmware
under `fw_spu/`. Upstream commit `0952e1c0` removed them from the tip, **not
from history**. Merely publishing a later ordinary commit would retain them.

An earlier publication used an orphan source snapshot on `buzz-quizworld`.
At the owner's request, the fork now uses normal `master` history, including
the original authored Buzz commits. The old branch remains as a backup;
it is no longer the port's development/submodule branch. No force push or
history deletion was used for this migration.

The existing upstream `master` ancestry is preserved. An explicit compatibility
commit restores the tested v0.17.0 tree before merging the original Buzz
lineage. This is not a v0.18.0 integration; a future upgrade must deliberately
revisit that compatibility commit and validate runtime/codegen/performance.
See `ps3recomp/BUZZ_FORK.md` for exact commits and provenance.

**This choice retains the inherited firmware objects in reachable history.**
A full SDK history audit is expected to flag those old paths. A clean current
tree audit is not a clean history audit or legal clearance. Restoring ancestry
does not newly add firmware to the current tree, remove previously hosted
objects from GitHub's fork network, or resolve their provenance/licensing.
Copied wiki PDF/text references and old diagnostic logs are absent from the
current tree but remain in history. Synthetic instruction/lifter fixtures
remain source tests, not retail captures. Full-history cloning is intentional.

## Reproducible pre-push checks

After committing the exact source changes intended for publication:

```powershell
.\toolenv\Scripts\python.exe tools/audit-publication.py --repo . --ref main --profile port
.\toolenv\Scripts\python.exe tools/audit-publication.py --repo ps3recomp --ref master --profile sdk --scope tip
# Separate inherited-history review: expect old upstream firmware findings.
.\toolenv\Scripts\python.exe tools/audit-publication.py --repo ps3recomp --ref master --profile sdk
git diff --check
git -C ps3recomp diff --check
git status --short
git -C ps3recomp status --short
```

Run the history audit from the full local working repository, not a shallow
checkout. It rejects known asset/output/private-config paths, common binary
formats, unexpected binary blobs, large blobs and common credential patterns.
It scans **all reachable blob contents** and historical changed paths for the
selected ref, including files deleted at the tip. `--scope tip` explicitly
limits the check to the current tree and labels that scope in the report;
it does not suppress or clear findings in the default history mode.
It checks submodules only
when invoked separately. It does not inspect unpushed refs, GitHub caches,
issues, releases or the fork's other branches. A clean result is a heuristic
check, **not** an exhaustive secret scanner, provenance proof or legal opinion.

Review file lists and source provenance manually too. Test the README flow in
a fresh source-only checkout with locally supplied inputs before announcing
build reproducibility. Do not create binary release assets from the game build.

Push SDK `master` first, then commit/push the port's exact gitlink pin.
Use only the named refs after confirming the remotes belong to the owner:

```powershell
git -C ps3recomp push origin master:refs/heads/master
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
