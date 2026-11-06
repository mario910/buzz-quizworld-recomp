#!/usr/bin/env python3
"""Match address-specific SPURS publications to observed consumer waits.

Requires BUZZ_PRODUCER_DEPENDENCY_TRACE and SPURS_JOB_TRACE_ALL=1.
Snapshots after DMA are observations, not an atomic history of guest writes.
Only 4/8-byte writes to previously watched lines are recorded. Missing matches
remain unknown; overlapping waits are never summed as a frame critical path.
"""
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict
import importlib.util
import json
from pathlib import Path
import re

spec=importlib.util.spec_from_file_location('producer_waits',Path(__file__).with_name('producer-waits.py'))
intervals=importlib.util.module_from_spec(spec);spec.loader.exec_module(intervals)

def read_events(path):
    events=[];header=''
    with Path(path).open() as stream:
        for line in stream:
            if line.startswith('#'):header+=line;continue
            if not line[:1].isdigit():continue
            k,who,a,b,start,end=line.split();k=int(k)
            if k in (8,16,18,100,110,112,113,114):
                events.append((k,int(who),int(a,16),int(b,16),float(start)/1000,float(end)/1000))
    dropped=re.search(r'dropped=(\d+)',header)
    if not dropped or int(dropped[1]):raise ValueError('Need explicit zero dropped records')
    return events

def analyze(events):
    events=sorted(events,key=lambda e:e[4])
    flips=[e[4] for e in events if e[0]==8]
    if len(flips)<2:raise ValueError('Need complete FIFO frame intervals')
    lo,hi=flips[0],flips[-1];frames=len(flips)-1
    jobs=defaultdict(list);words=defaultdict(list);tails={};jumps=defaultdict(list)
    for e in events:
        if e[0]==16:jobs[e[1]].append(e)
        elif e[0]==112:words[e[2]].append(e)
        elif e[0]==113:tails[(e[1],e[2]-4,e[4])]=e[3]
        elif e[0]==114:jumps[e[2]].append(e)
    job_starts={tid:[e[4] for e in rows] for tid,rows in jobs.items()}
    word_starts={ea:[e[4] for e in rows] for ea,rows in words.items()}
    jump_starts={ea:[e[4] for e in rows] for ea,rows in jumps.items()}
    def owner(e):
        rows=jobs.get(e[1],[]);i=bisect_right(job_starts.get(e[1],[]),e[4])-1
        return rows[i][2] if i>=0 and rows[i][4]<=e[4]<=rows[i][5] else None
    def writes(ea,start,end):
        rows=words.get(ea,[]);times=word_starts.get(ea,[])
        return rows[bisect_left(times,start):bisect_right(times,end)]
    barriers=[e for e in events if e[0] in (8,110)]
    barrier_times=[e[4] for e in barriers]
    parks=[];unmatched=0;consumed={}
    for e in events:
        if e[0]!=110 or not lo<=e[4]<hi:continue
        ea=e[2]
        if e[4]<consumed.get(ea,0):continue # repeated observation of one park
        rows=jumps.get(ea,[]);i=bisect_left(jump_starts.get(ea,[]),e[4])
        # A later flip or a different park proves the consumer moved on.
        # Do not accidentally match a reused ring address in a later frame.
        horizon=hi
        for barrier in barriers[bisect_right(barrier_times,e[4]):]:
            if barrier[0]==8 or barrier[2]!=ea:
                horizon=min(horizon,barrier[4]);break
        if i==len(rows) or rows[i][4]>horizon:unmatched+=1;continue
        end=rows[i][4];consumed[ea]=end
        candidates=[w for w in writes(ea,e[4],end) if w[3]!=e[3]]
        if not candidates:unmatched+=1;continue
        w=candidates[0]
        parks.append(dict(address=hex(ea),start=e[4],published=w[4],consumed=end,
                          image=owner(w),published_word=hex(w[3]),consumed_word=hex(rows[i][3]),
                          same_word=w[3]==rows[i][3]))
    jts=[];unmatched_jts=0
    for e in events:
        if e[0]!=18 or e[4]<lo or e[5]>hi:continue
        # Only a complete non-JTS 64-bit replacement of the waited PC is
        # accepted here. A root redirect is a separate unmeasured dependency.
        candidates=[]
        for w in writes(e[3],e[4],e[5]):
            tail=tails.get((w[1],w[2],w[4]))
            if tail is not None and (w[3],tail)!=(8,0x12):candidates.append(w)
        if not candidates:unmatched_jts+=1;continue
        w=candidates[0]
        jts.append(dict(chain=hex(e[2]),pc=hex(e[3]),start=e[4],published=w[4],
                        resumed=e[5],image=owner(w)))
    idle=intervals.union([(max(lo,e[4]),min(hi,e[5])) for e in events if e[0]==100 and e[5]>lo and e[4]<hi])
    def summarize(rows,end_key):
        delays=sorted(r[end_key]-r['published'] for r in rows)
        before=[(r['start'],r['published']) for r in rows]
        after=[(r['published'],r[end_key]) for r in rows]
        return dict(matches=len(rows),images=dict(Counter(str(r['image']) for r in rows)),
                    mean_publish_to_resume_us=1000*sum(delays)/len(delays) if delays else None,
                    p95_publish_to_resume_us=1000*delays[int(.95*(len(delays)-1))] if delays else None,
                    idle_before_publication_ms_per_frame=intervals.overlap(idle,before)/frames,
                    idle_after_publication_ms_per_frame=intervals.overlap(idle,after)/frames)
    return dict(frames=frames,window_ms=hi-lo,idle_ms_per_frame=sum(b-a for a,b in idle)/frames,
                parks=dict(**summarize(parks,'consumed'),unmatched=unmatched,
                           same_word_matches=sum(r['same_word'] for r in parks)),
                chain_pc=dict(**summarize(jts,'resumed'),unmatched_wait_spans=unmatched_jts),
                park_details=parks,chain_details=jts,
                limitations=['Only observed small DMA publications are matched; absence is unknown.',
                             'A post-copy word snapshot can race another writer.',
                             'Consumer latency includes scheduling and any other work, not just notification.',
                             'Idle overlaps across categories must not be summed; this is not FPS.'])

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('trace',type=Path)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    result=analyze(read_events(args.trace));args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if not k.endswith('_details')},indent=2))
