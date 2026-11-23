#!/bin/sh
# Linux counterpart of Launch-Buzz.cmd: start the prepared standalone game,
# or the native launcher when only a developer build exists.
here=$(dirname "$(readlink -f "$0")")
if [ "${1:-}" = "--steam-deck" ]; then
    shift
fi
if [ -x "$here/dist/BuzzQuizWorldRecomp" ]; then
    cd "$here/dist" && exec ./BuzzQuizWorldRecomp "$@"
fi
for launcher in "$here/build-launcher/BuzzLauncher" "$here/build-launcher-linux/BuzzLauncher"; do
    [ -x "$launcher" ] && exec "$launcher" --project "$here" "$@"
done
echo "No prepared game yet. Run ./Prepare-Buzz.sh, or build the launcher with: bash scripts/build-launcher-linux.sh"
exit 1
