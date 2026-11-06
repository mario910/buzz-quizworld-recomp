"""Associate exact PPU busy-word wakes with their publishing SPURS jobs.

Requires BUZZ_PPU_WORD_WAIT_TRACE and SPURS_JOB_TRACE_ALL. Diagnostic wall
spans include scheduling and nested waits; these are not CPU percentages.
"""
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict
import json
from pathlib import Path
import re


def read_events(path):
    events=[];headers=''
    with Path(path).open() as stream:
        for line in stream:
            if line.startswith('#'):headers+=line;continue
            if not line[:1].isdigit():continue
            kind,tid,ea,value,start,end=line.split();kind=int(kind)
            if kind in (16,115,116):
                row=(kind,int(tid),int(ea,16),int(value,16),float(start),float(end))
                if row[5]<row[4]:raise ValueError('Reversed trace span')
                events.append(row)
    drops=re.findall(r'dropped=(\d+)',headers)
    if len(drops)!=1 or int(drops[0]):raise ValueError('Require explicit zero dropped records')
    bounds=re.search(r'start_us=([\d.]+) end_us=([\d.]+)',headers)
    if not bounds:raise ValueError('Need capture bounds')
    lo,hi=map(float,bounds.groups())
    if hi<=lo:raise ValueError('Invalid capture window')
    # Incomplete wait/job spans at capture edges cannot establish ownership.
    return [e for e in events if lo<=e[4]<=e[5]<=hi]


def analyze(events):
    jobs=defaultdict(list);writes=defaultdict(list);waits=[]
    for e in sorted(events,key=lambda e:e[4]):
        if e[0]==16:jobs[e[1]].append(e)
        elif e[0]==116 and e[3]==0:writes[e[2]].append(e)
        elif e[0]==115:waits.append(e)
    jt={tid:[e[4] for e in rows] for tid,rows in jobs.items()}
    wt={ea:[e[4] for e in rows] for ea,rows in writes.items()}
    matched=[];unmatched=0
    for wait in waits:
        candidates=writes.get(wait[2],[])[bisect_left(wt.get(wait[2],[]),wait[4]):
                                               bisect_right(wt.get(wait[2],[]),wait[5])]
        if not candidates:unmatched+=1;continue
        # Last observed zero publication during this wait; do not match a
        # prior use of the same address or an unrelated thread's job.
        pub=candidates[-1];rows=jobs.get(pub[1],[])
        i=bisect_right(jt.get(pub[1],[]),pub[4])-1
        job=rows[i] if i>=0 and rows[i][4]<=pub[4]<=rows[i][5] else None
        matched.append(dict(address=hex(wait[2]),caller=hex(wait[3]),
            image=job[2] if job else None,wait_us=wait[5]-wait[4],
            before_publication_us=pub[4]-wait[4],publish_to_resume_us=wait[5]-pub[4],
            before_job_us=max(0,job[4]-wait[4]) if job else None,
            inside_job_us=pub[4]-max(wait[4],job[4]) if job else None))
    def stats(key):
        values=sorted(x[key] for x in matched if x[key] is not None)
        return dict(mean_us=sum(values)/len(values),p95_us=values[int(.95*(len(values)-1))],
                    max_us=values[-1]) if values else None
    return dict(waits=len(waits),matched=len(matched),unmatched=unmatched,
        images=dict(Counter(str(x['image']) for x in matched)),
        before_publication=stats('before_publication_us'),publish_to_resume=stats('publish_to_resume_us'),
        before_publishing_job=stats('before_job_us'),inside_publishing_job=stats('inside_job_us'),
        observations=matched,
        limitation='Instrumented wall spans, not CPU/FPS; unmatched publications and unknown job ownership remain unassigned.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=analyze(read_events(a.trace));a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='observations'},indent=2))
