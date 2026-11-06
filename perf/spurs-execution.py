"""Attribute diagnostic SPURS execution spans to CPU counters and channel waits.

Windows event 117 counts scheduled thread cycles; POSIX counts thread CPU ns.
Cycle density is not CPU utilization. Instrumentation changes scheduling.
"""
import argparse
from collections import defaultdict
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('overlaps',Path(__file__).with_name('producer-waits.py'))
overlaps=importlib.util.module_from_spec(spec);spec.loader.exec_module(overlaps)

def summarize(events):
    jobs=defaultdict(list);waits=defaultdict(list);idle=[]
    for k,tid,a,b,start,end in events:
        if k==117:jobs[a].append((tid,b,start,end))
        elif k==118:waits[tid].append((start,end))
        elif k==100:idle.append((start,end))
    result={}
    for image,rows in jobs.items():
        durations=sorted(end-start for _,_,start,end in rows)
        valid=[x for x in rows if x[1]!=0xffffffff and x[3]>x[2]]
        densities=sorted(cpu/((end-start)*1000) for _,cpu,start,end in valid)
        wait_us=sum(overlaps.overlap([(start,end)],waits[tid]) for tid,_,start,end in rows)
        def stats(v):
            return {'mean':sum(v)/len(v),'median':v[len(v)//2], 'p95':v[int(.95*(len(v)-1))],'max':v[-1]} if v else None
        result[str(image)]={'jobs':len(rows),'wall_us':stats(durations),'cpu_units':sum(x[1] for x in valid),
            'invalid_cpu_counts':len(rows)-len(valid),'cpu_units_per_wall_ns':stats(densities),
            'blocked_channel_us':wait_us,'blocked_channel_percent_of_job_wall':100*wait_us/sum(durations) if sum(durations) else 0,
            'union_execution_us':sum(e-s for s,e in overlaps.union([(s,e) for _,_,s,e in rows])),
            'rsx_idle_overlap_us':overlaps.overlap(idle,[(s,e) for _,_,s,e in rows])}
    return result

def read(path):
    import re
    events=[];header=''
    with Path(path).open() as f:
        for line in f:
            if line.startswith('#'):header+=line;continue
            if not line[:1].isdigit():continue
            k,tid,a,b,start,end=line.split()
            if int(k) in (100,117,118):events.append((int(k),int(tid),int(a,16),int(b,16),float(start),float(end)))
    drops=re.findall(r'dropped=(\d+)',header)
    if drops!=['0']:raise ValueError('Require explicit zero dropped records')
    bounds=re.search(r'start_us=([\d.]+) end_us=([\d.]+)',header)
    if not bounds:raise ValueError('Missing capture bounds')
    lo,hi=map(float,bounds.groups())
    if hi<=lo:raise ValueError('Invalid capture window')
    if any(x[5]<x[4] for x in events):raise ValueError('Reversed span')
    return [x for x in events if lo<=x[4]<=x[5]<=hi]

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path)
    p.add_argument('--cpu-unit',choices=('cycles','nanoseconds'),required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out={'cpu_unit':a.cpu_unit,'images':summarize(read(a.trace)),
       'limitation':'Instrumented wall spans include scheduling. Unrecorded polling/locks are not covered by channel waits. Cycles are not CPU nanoseconds.'}
    a.output.write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
