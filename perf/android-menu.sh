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
$ADB shell "nohup sh /data/local/tmp/buzz-sup.sh $LIMIT 142 /data/local/tmp/buzz-run >/dev/null 2>&1 &"
sleep 1
mask_args=(); [ "$MASK" != 0 ] && mask_args=(--ei buzz_test_cpu_mask "$MASK")
$ADB shell am start -n $P/.BuzzActivity --ei buzz_test_seconds 140 --ez buzz_test_skip_intro true \
  --ez buzz_test_input_file true "${mask_args[@]}" "$@" >/dev/null
# Press red until the main menu is up, instead of at fixed times: boot time
# varies, and a press that lands before the language screen is lost. The
# language and buzzer-info screens present a steady 30 FPS; the studio menu
# behind "Co chcesz zrobic?" drops well below. Check the last 3 s of the live
# frame CSV after each press; stop after two consecutive slow readings.
frames_last3() {
  $ADB shell run-as $P cat files/runtime/frame-times.csv 2>/dev/null | tr -d '\r' |
    awk -F, 'NR>1 && $3+0>0 {t[NR]=$3} END {n=0; for (i in t) if (t[i] > last) last=t[i];
             for (i in t) if (t[i] > last-3000) n++; print n/3, last/1000}'
}
start=$(date +%s); presses=0; slow=0; menu_at=
sleep 15
while [ $(( $(date +%s) - start )) -lt 75 ]; do
  set -- $(frames_last3); fps=${1:-0}; at=${2:-0}
  if [ "$presses" -ge 2 ] && awk "BEGIN{exit !($fps < 22)}"; then
    slow=$((slow + 1)); [ $slow -ge 2 ] && { menu_at=$at; break; }
  else
    slow=0
    if [ "$presses" -lt 8 ] && awk "BEGIN{exit !($fps >= 22)}"; then
      ADB=$ADB "$HERE/android-press.sh" -g 0 P1r; presses=$((presses + 1))
      echo "press $presses at game time ${at}s (${fps} FPS)"
    fi
  fi
  sleep 4
done
if [ -z "$menu_at" ]; then
  echo "MENU NOT REACHED after $presses presses (last ${fps} FPS at ${at}s); stopping"
  $ADB shell am force-stop $P
fi
echo "menu from game time ${menu_at:-?}s"
# Measure 30 s starting 10 s after the menu was detected.
sleep 42
$ADB shell cat /data/local/tmp/buzz-run/end.txt
rm -rf "$OUT/buzz-run"; $ADB exec-out "tar -C /data/local/tmp -cf - buzz-run" | tar -C "$OUT" -xf -
$ADB exec-out run-as $P cat files/runtime/frame-times.csv > "$OUT/frame-times.csv"
$ADB exec-out run-as $P cat files/runtime/game-error.log > "$OUT/game-error.log"
$ADB shell run-as $P rm -f files/runtime/profile-frames
W=$OUT; command -v cygpath >/dev/null && { W=$(cygpath -w "$OUT"); HW=$(cygpath -w "$HERE"); } || HW=$HERE
python - "$W" "${menu_at:-}" <<'PY'
import csv, sys, os
rows = list(csv.DictReader(open(os.path.join(sys.argv[1], 'frame-times.csv'))))
a = float(sys.argv[2]) * 1000 + 10000 if len(sys.argv) > 2 and sys.argv[2] not in ('', '?') else 62000
win = [r for r in rows if a <= float(r['elapsed_ms']) < a + 30000]
iv = sorted(float(r['interval_ms']) for r in win)
rows_t = [l.split() for l in open(os.path.join(sys.argv[1], 'buzz-run', 'temps.txt'))]
head = rows_t[0][4:] if rows_t and rows_t[0][0] == '#' else []
rows_t = [r for r in rows_t if r[0] != '#']
temps = [int(r[1]) for r in rows_t]
t0 = float(rows_t[0][0]) if rows_t else 0
inwin = [r for r in rows_t if a/1000 <= float(r[0]) - t0 <= a/1000 + 30 and len(r) > 3]
if inwin and head:
    print('mean frequency in window: ' + ', '.join(f"{name} {sum(int(r[3 + i]) for r in inwin) / len(inwin) / 1e6:.2f} GHz" for i, name in enumerate(head)))
if win: print(f"menu {a/1000:.0f}-{a/1000+30:.0f} s: {len(win)/30:.2f} FPS, median {iv[len(iv)//2]:.1f} ms, p95 {iv[int(len(iv)*.95)]:.1f} ms; max temp {max(temps)/1000:.1f} C")
else: print("no frames in window; last elapsed", rows[-1]['elapsed_ms'] if rows else None)
PY
m=${menu_at:-52}; python "$HW/android-threads.py" "$W" --from ${m%.*} --to $(( ${m%.*} + 40 )) | sed -n '1p;$p'
