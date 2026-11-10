#!/usr/bin/env bash
# Bounded Thor/Android test run with the device-local supervisor.
#   android-run.sh <tag> [cpu_mask] [seconds] [limit_C] [extra am args...]
# Results land in ../android-runs/<tag> (frames CSV, temps, per-thread CPU, logs).
set -u
export MSYS_NO_PATHCONV=1
ADB=${ADB:-adb}
TAG=$1; MASK=${2:-31}; SECS=${3:-60}; LIMIT=${4:-78}; shift $(( $# < 4 ? $# : 4 ))
OUT="$(dirname "$0")/../../android-runs/$TAG"; mkdir -p "$OUT"
PKG=org.buzzrecomp.game
$ADB shell am force-stop $PKG
$ADB shell input keyevent WAKEUP; $ADB shell wm dismiss-keyguard >/dev/null 2>&1
$ADB shell run-as $PKG touch files/runtime/profile-frames
$ADB shell run-as $PKG rm -f files/runtime/frame-times.csv
$ADB shell "nohup sh /data/local/tmp/buzz-sup.sh $LIMIT $((SECS + 2)) /data/local/tmp/buzz-run >/dev/null 2>&1 &"
sleep 1
mask_args=(); [ "$MASK" != 0 ] && mask_args=(--ei buzz_test_cpu_mask "$MASK")
$ADB shell am start -n $PKG/.BuzzActivity --ei buzz_test_seconds "$SECS" --ez buzz_test_skip_intro true "${mask_args[@]}" "$@" >/dev/null
sleep $((SECS + 6))
$ADB shell cat /data/local/tmp/buzz-run/end.txt
$ADB exec-out "tar -C /data/local/tmp -cf - buzz-run" | tar -C "$OUT" -xf -
$ADB exec-out run-as $PKG cat files/runtime/frame-times.csv > "$OUT/frame-times.csv"
$ADB exec-out run-as $PKG cat files/runtime/game.log > "$OUT/game.log"
$ADB shell run-as $PKG rm -f files/runtime/profile-frames
echo "results in $OUT"
