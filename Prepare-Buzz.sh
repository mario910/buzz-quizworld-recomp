#!/bin/sh
# Linux counterpart of Prepare-Buzz.cmd: open the BuzzSetup preparation wizard.
here=$(dirname "$(readlink -f "$0")")
for wizard in "$here/dist-preparation/linux/bin/BuzzPrepare" "$here/build-preparation/linux/BuzzPrepare"; do
    [ -x "$wizard" ] && exec "$wizard" "$@"
done
echo "BuzzPrepare is missing. Download the BuzzPrepare Linux package from the releases page,"
echo "or build it with: bash scripts/build-preparation-linux.sh"
exit 1
