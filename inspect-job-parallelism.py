"""Inspect recorded SPU-job overlap; wall-clock spans are not CPU utilization."""
import collections
from pathlib import Path
import re
import sys

path=Path(sys.argv[1])
image=int(sys.argv[2]) if len(sys.argv)>2 else 41
events=[]
threads=collections.Counter()
total=0.0
with path.open() as stream:
    first=stream.readline()
    window=re.search(r'start_us=([\d.]+) end_us=([\d.]+)',first)
    lo,hi=map(float,window.groups())
    for line in stream:
        fields=line.split()
        if len(fields)!=6 or fields[0]!='16' or int(fields[2],16)!=image:
            continue
        start,end=max(lo,float(fields[4])),min(hi,float(fields[5]))
        if end<=start:continue
        events.extend(((start,1),(end,-1)))
        threads[int(fields[1])]+=1
        total+=end-start
events.sort()
occupied=collections.Counter()
last=lo
active=0
peak=0
for point,delta in events:
    occupied[active]+=point-last
    active+=delta
    peak=max(peak,active)
    last=point
occupied[active]+=hi-last
assert active==0
busy=sum(duration for count,duration in occupied.items() if count)
print(f'{path.name}: image={image} jobs={sum(threads.values())} window={(hi-lo)/1e6:.3f}s')
print(f'summed job spans={total/1e6:.3f}s union={busy/1e6:.3f}s mean overlap when active={total/busy if busy else 0:.2f}, peak={peak}')
for count,duration in sorted(occupied.items()):
    print(f'  {count} jobs active: {100*duration/(hi-lo):.2f}% wall time')
print('Jobs per native thread:',threads.most_common())
print('Shorter-than-threshold jobs are absent; overlap includes descheduling/waits, not just execution.')
