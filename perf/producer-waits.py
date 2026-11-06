#!/usr/bin/env python3
"""Measure overlap of RSX idle intervals and in-flight SPURS jobs/chains.

Overlap is elapsed wall time, not CPU utilization or proof of causality.
Parallel job spans are unioned before comparing them with RSX idle time.
"""
import argparse
from pathlib import Path
import json
import re

def union(spans):
    result=[]
    for start,end in sorted(spans):
        if end<=start:continue
        if result and start<=result[-1][1]:
            result[-1]=(result[-1][0],max(result[-1][1],end))
        else:result.append((start,end))
    return result

def overlap(left,right):
    left,right=union(left),union(right)
    total=0.;i=j=0
    while i<len(left) and j<len(right):
        a,b=left[i];c,d=right[j]
        total+=max(0,min(b,d)-max(a,c))
        if b<d:i+=1
        else:j+=1
    return total

def analyze(path):
    events=[];header=''
    for line in path.read_text().splitlines():
        if line.startswith('#'):header+=line;continue
        if not line.strip() or line.startswith('T '):continue
        k,who,a,b,start,end=line.split()
        events.append((int(k),int(who),int(a,16),int(b,16),float(start)/1000,float(end)/1000))
    dropped=re.search(r'dropped=(\d+)',header)
    if dropped and int(dropped[1]):raise ValueError('Trace lost records')
    events.sort(key=lambda e:e[4])
    flips=[e[4] for e in events if e[0]==8]
    if len(flips)<2:raise ValueError('Need two complete FIFO flips')
    lo,hi=flips[0],flips[-1];frames=len(flips)-1
    def spans(kind,a=None):
        return [(max(lo,e[4]),min(hi,e[5])) for e in events
                if e[0]==kind and (a is None or e[2]==a) and e[5]>lo and e[4]<hi]
    idle=union(spans(100));idle_ms=sum(b-a for a,b in idle)
    groups={'any_job':spans(16),'image39_job':spans(16,39),'image41_job':spans(16,41),
            'chain_waiting_publication':spans(18),'chain_guard_wait':spans(19),
            'parallel_job_group':spans(20),'pool_queue_to_first_claim':spans(23),
            'pool_last_done_to_return':spans(24)}
    result={}
    for name,activity in groups.items():
        busy=overlap(idle,activity)
        result[name]={'union_ms_per_frame':sum(b-a for a,b in union(activity))/frames,
                      'overlap_idle_ms_per_frame':busy/frames,
                      'percent_of_idle':100*busy/idle_ms if idle_ms else 0}
    return {'trace':str(path),'frames':frames,'window_ms':hi-lo,
            'idle_ms_per_frame':idle_ms/frames,'activity':result,
            'notes':['In-flight job time may itself contain waits; it is not CPU utilization.',
                     'Categories overlap: never add their times or percentages.',
                     'Record absence below the trace duration threshold is not proof of inactivity.',
                     'Use a separate trace-disabled series for FPS.']}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    text=json.dumps(analyze(args.trace),indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text)
