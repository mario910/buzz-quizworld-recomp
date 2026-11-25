# Release notes

A `v*` tag builds BuzzSetup and publishes the GitHub release
(`.github/workflows/native-preparation.yml`). When `docs/releases/<tag>.md`
exists in the tagged commit, it opens the release notes; the install
instructions and GitHub's generated list of changes follow it.

To write the notes for the next release:

1. Add `docs/releases/vX.Y.Z.md` (English, Markdown) and its screenshots in
   `docs/releases/vX.Y.Z/` (JPEG or PNG, a few hundred KB each).
2. Reference images by their full URL pinned to the tag, since relative paths
   do not resolve in release notes:
   `https://github.com/mario910/buzz-quizworld-recomp/raw/vX.Y.Z/docs/releases/vX.Y.Z/name.jpg`
   (they appear once the tag is pushed).
3. Commit, then tag that commit and push the tag.

The notes can still be edited on GitHub after the release is published.
