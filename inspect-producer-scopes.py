"""Selected PPU function lifetimes, nesting and overlap; not CPU utilization."""
import collections
import importlib.util
from pathlib import Path
import statistics
import sys

spec=importlib.util.spec_from_file_location('dependencies',Path(__file__).with_name('inspect-geometry-dependencies.py'))
intervals=importlib.util.module_from_spec(spec);spec.loader.exec_module(intervals)


def exclusive_scopes(scopes):
    """Return pieces outside nested selected scopes, never subtract a parent."""
    by_thread=collections.defaultdict(list)
    for index,r in enumerate(scopes):by_thread[r[1]].append((index,r))
    children=collections.defaultdict(list);crossings=0
    for rows in by_thread.values():
        stack=[]
        for index,r in sorted(rows,key=lambda pair:(pair[1][4],-pair[1][5])):
            while stack and r[4]>=stack[-1][1][5]:stack.pop()
            if stack:
                parent_index,parent=stack[-1]
                if r[5]<=parent[5]:children[parent_index].append((r[4],r[5]))
                else:crossings+=1
            stack.append((index,r))
    out=collections.defaultdict(list)
    for index,r in enumerate(scopes):
        out[(r[1],r[2])].extend(intervals.subtract([(r[4],r[5])],children[index]))
    return out,crossings


def main(path):
    names={};records=[]
    with open(path) as source:
        print(source.readline().strip())
        for line in source:
            fields=line.split()
            if fields and fields[0]=='T':names[int(fields[1])]=' '.join(fields[2:])
            elif len(fields)==6 and fields[0].isdigit():
                k,tid=map(int,fields[:2]);a,b=(int(v,16) for v in fields[2:4]);lo,hi=map(float,fields[4:]);records.append((k,tid,a,b,lo,hi))
    presents=sorted(r[5] for r in records if r[0]==10)
    if len(presents)<2:raise ValueError('Need at least two completed host presents')
    window=[(presents[0],presents[-1])];frames=len(presents)-1
    scopes=[r for r in records if r[0]==26 and r[5]>window[0][0] and r[4]<window[0][1]]
    if not scopes:raise ValueError('No selected PPU function scopes; use -ProducerTrace')
    exclusive,crossings=exclusive_scopes(scopes)
    if crossings:raise ValueError(f'Crossing scopes on one PPU thread: {crossings}')
    groups=collections.defaultdict(list)
    for r in scopes:groups[(r[1],r[2])].append(r)
    main_wait=intervals.union((r[4],r[5]) for r in records if r[0]==1 and r[1]==1 and r[3]==0x2521f0)
    main_wait=intervals.subtract(main_wait,[(float('-inf'),window[0][0]),(window[0][1],float('inf'))])
    jobs=intervals.union((r[4],r[5]) for r in records if r[0]==16)
    outside_jobs=intervals.subtract(main_wait,jobs)
    print(f'{frames} host intervals, main wait={intervals.elapsed(main_wait)/frames/1000:.2f} ms/frame; '
          f'outside recorded jobs={intervals.elapsed(outside_jobs)/frames/1000:.2f} ms/frame')
    print('Times are wall-clock intervals including unprofiled callees/waits, NOT CPU costs.')
    ordered=sorted(groups,key=lambda key:intervals.overlap(intervals.union(exclusive[key]),window),reverse=True)
    for key in ordered:
        tid,pc=key;rows=groups[key]
        inclusive=intervals.union((r[4],r[5]) for r in rows);pieces=intervals.union(exclusive[key])
        elapsed=intervals.overlap(inclusive,window);own=intervals.overlap(pieces,window)
        wait=intervals.overlap(pieces,main_wait);missing=intervals.overlap(pieces,outside_jobs)
        durations=[r[5]-r[4] for r in rows if r[4]>=window[0][0] and r[5]<=window[0][1]]
        med=statistics.median(durations) if durations else float('nan')
        callers=collections.Counter(r[3] for r in rows)
        print(f'tid={tid} {names.get(tid,"?")} pc={pc:08X} calls/frame={len(rows)/frames:.1f} median={med:.1f} us '
              f'inclusive={elapsed/frames/1000:.2f} outside-nested={own/frames/1000:.2f} '
              f'main-wait-overlap={wait/frames/1000:.2f} main-wait-outside-jobs={missing/frames/1000:.2f} ms/frame')
        print('  callers: '+', '.join(f'{lr:08X}:{count}' for lr,count in callers.most_common(4)))
    print('Inclusive scopes overlap. Short jobs may be filtered; taskset/video and instrumentation overhead are not separately attributed.')


if __name__=='__main__':main(sys.argv[1])
