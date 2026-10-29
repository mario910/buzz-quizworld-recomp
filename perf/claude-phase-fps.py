"""Per-second presented FPS of PS3_FTRACE traces whose windows share the menu
camera phase; prints the heavy (0-9 s) and light (11-16 s) segment means."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import sys
for path in sys.argv[1:]:
    pres = sorted(float(l.split()[4]) for l in open(path) if l.startswith("10 "))
    t0 = pres[0]
    row = [sum(1 for p in pres if t0 + s * 1e6 <= p < t0 + (s + 1) * 1e6) for s in range(20)]
    print(f"{path.replace(chr(92), '/').split('/')[-1]:34} heavy0-9s={sum(row[0:9])/9:5.1f} "
          f"light11-16s={sum(row[11:16])/5:5.1f} all={len(pres)/20:5.2f}  {' '.join(f'{x:2d}' for x in row)}")
