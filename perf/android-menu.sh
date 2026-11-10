#!/usr/bin/env bash
# Matched Thor main-menu measurement: boot (startup video skipped), press red
# through language + buzzer info, then record seconds 62-92 of the run.
#   android-menu.sh <tag> [cpu_mask] [limit_C] [extra am args...]
# Prints menu FPS (frame CSV) and process CPU (supervisor task snapshots).
set -u
export MSYS_NO_PATHCONV=1
ADB=${ADB:-adb}; HERE=$(cd "$(dirname "$0")" && pwd)
TAG=$1; MASK=${2:-31}; LIMIT=${3:-80}; shift $(( $# < 3 ? $# : 3 ))
OUT="$HERE/../../android-runs/$TAG"; mkdir -p "$OUT"
P=org.buzzrecomp.game
$ADB shell am force-stop $P
$ADB shell input keyevent WAKEUP
$ADB shell run-as $P touch files/runtime/profile-frames
$ADB shell run-as $P rm -f files/runtime/frame-times.csv
$ADB shell "nohup sh /data/local/tmp/buzz-sup.sh $LIMIT 97 /data/local/tmp/buzz-run >/dev/null 2>&1 &"
sleep 1
mask_args=(); [ "$MASK" != 0 ] && mask_args=(--ei buzz_test_cpu_mask "$MASK")
$ADB shell am start -n $P/.BuzzActivity --ei buzz_test_seconds 95 --ez buzz_test_skip_intro true \
  --ez buzz_test_input_file true "${mask_args[@]}" "$@" >/dev/null
sleep 22; ADB=$ADB "$HERE/android-press.sh" -g 6 P1r; ADB=$ADB "$HERE/android-press.sh" -g 1 P1r
sleep 72
$ADB shell cat /data/local/tmp/buzz-run/end.txt
rm -rf "$OUT/buzz-run"; $ADB exec-out "tar -C /data/local/tmp -cf - buzz-run" | tar -C "$OUT" -xf -
$ADB exec-out run-as $P cat files/runtime/frame-times.csv > "$OUT/frame-times.csv"
$ADB exec-out run-as $P cat files/runtime/game-error.log > "$OUT/game-error.log"
$ADB shell run-as $P rm -f files/runtime/profile-frames
W=$OUT; command -v cygpath >/dev/null && { W=$(cygpath -w "$OUT"); HW=$(cygpath -w "$HERE"); } || HW=$HERE
python - "$W" <<'PY'
import csv, sys, os
rows = list(csv.DictReader(open(os.path.join(sys.argv[1], 'frame-times.csv'))))
win = [r for r in rows if 62000 <= float(r['elapsed_ms']) < 92000]
iv = sorted(float(r['interval_ms']) for r in win)
temps = [int(l.split()[1]) for l in open(os.path.join(sys.argv[1], 'buzz-run', 'temps.txt'))]
if win: print(f"menu 62-92 s: {len(win)/30:.2f} FPS, median {iv[len(iv)//2]:.1f} ms, p95 {iv[int(len(iv)*.95)]:.1f} ms; max temp {max(temps)/1000:.1f} C")
else: print("no frames in window; last elapsed", rows[-1]['elapsed_ms'] if rows else None)
PY
python "$HW/android-threads.py" "$W" --from 60 --to 92 | sed -n '1p;$p'
