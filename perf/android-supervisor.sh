#!/system/bin/sh
# Device-local guard + per-thread CPU recorder for bounded Android test runs.
# Runs on the phone (adb push to /data/local/tmp, started with nohup) so it
# keeps working if ADB stalls under load.
#   android-supervisor.sh <limit_C> <deadline_s> <outdir>
# Stops org.buzzrecomp.game when any CPU/GPU sensor reaches <limit_C> or at
# <deadline_s> after start. About every 0.3 s appends "uptime maxtemp" to temps.txt;
# about every 3 s writes tasks-<uptime>.txt: raw /proc/<pid>/task/*/stat lines
# (perf/android-threads.py turns them into per-thread CPU%).
LIMIT=${1:-63}; DEADLINE=${2:-60}; OUT=${3:-/data/local/tmp/buzz-run}
PKG=org.buzzrecomp.game
mkdir -p "$OUT"; rm -f "$OUT"/*
ZONES=""
for z in /sys/class/thermal/thermal_zone*; do
  case "$(cat $z/type 2>/dev/null)" in cpu*|gpuss*) ZONES="$ZONES $z/temp";; esac
done
start=$(cut -d. -f1 /proc/uptime)
pid=""; n=0
while :; do
  read now _ < /proc/uptime
  [ -z "$pid" ] && pid=$(pidof $PKG)
  max=0
  for t in $ZONES; do v=; read v < $t 2>/dev/null; [ -n "$v" ] && [ "$v" -gt "$max" ] && max=$v; done
  echo "$now $max ${pid:-none}" >> "$OUT/temps.txt"
  if [ "$max" -ge "${LIMIT}000" ]; then
    am force-stop $PKG; echo "KILL temp $max at $now" >> "$OUT/end.txt"; exit 0
  fi
  if [ $(( ${now%.*} - start )) -ge "$DEADLINE" ]; then
    am force-stop $PKG; echo "DEADLINE at $now" >> "$OUT/end.txt"; exit 0
  fi
  if [ -n "$pid" ] && [ ! -d /proc/$pid ]; then echo "EXITED at $now" >> "$OUT/end.txt"; exit 0; fi
  n=$((n + 1))
  if [ -n "$pid" ] && [ $((n % 10)) -eq 0 ]; then
    # One fork: raw /proc/<pid>/task/*/stat lines, parsed on the host.
    cat /proc/$pid/task/*/stat > "$OUT/tasks-$now.txt" 2>/dev/null
  fi
  sleep 0.25
done
