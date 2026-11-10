#!/usr/bin/env bash
# Screenshot of the game's display: android-shot.sh out.png [display_id]
# Dual-screen handhelds need the id of the display the game is on
# (adb shell dumpsys SurfaceFlinger --display-id); default: the first one.
export MSYS_NO_PATHCONV=1
ADB=${ADB:-adb}
ID=${2:-$($ADB shell dumpsys SurfaceFlinger --display-id | sed -n '1s/^Display \([0-9]*\).*/\1/p')}
$ADB exec-out screencap -p ${ID:+-d $ID} > "$1"
