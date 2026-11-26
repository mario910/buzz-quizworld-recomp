#!/bin/sh
# Linux counterpart of Launch-Buzz.cmd: start the game prepared by BuzzSetup,
# otherwise open BuzzSetup to prepare it.
here=$(dirname "$(readlink -f "$0")")
if [ "${1:-}" = "--steam-deck" ]; then
    shift
    # Older prepared binaries still need the explicit studio environment.
    export BUZZ_STUDIO_DETAIL="${BUZZ_STUDIO_DETAIL:-balanced}" BUZZ_PRESENT_HOLD_MS="${BUZZ_PRESENT_HOLD_MS:-0}"
fi
if [ -x "$here/dist/BuzzQuizWorldRecomp" ]; then
    cd "$here/dist" && exec ./BuzzQuizWorldRecomp "$@"
fi
echo "No prepared game yet: opening BuzzSetup."
exec "$here/Prepare-Buzz.sh"
