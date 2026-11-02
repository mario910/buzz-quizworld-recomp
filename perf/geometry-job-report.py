"""Validate one same-EXE 0/1/1/0 series: whole JOB or one option with JOB1."""
import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import statistics


def load(name, filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


ab=load('geometry_ab','geometry-ab-report.py')
work=load('process_work','measure-process-work.py')
readback=load('frame_readback','frame-readback.py')
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()


def comparison(values):
    if any(v<=0 for v in values):raise ValueError('Nonpositive measurement')
    baseline=statistics.mean(values[i] for i in (0,3));native=statistics.mean(values[i] for i in (1,2))
    return dict(baseline=baseline,native=native,change_percent=100*(native/baseline-1),
        pair_changes_percent=[100*(values[b]/values[a]-1) for a,b in ((0,1),(3,2))])


def report(progress,csv_directory,scene_qa=None):
    events=[json.loads(line) for line in progress.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    if len(events)!=8 or [e['state'] for e in events]!=['start','done']*4:raise ValueError('Need four complete sequential runs')
    starts=[e['run'] for e in events[::2]];done=events[1::2];runs=[e['run'] for e in done]
    if starts!=runs or len({r['tag'] for r in runs})!=4:raise ValueError('Duplicate or changed run metadata')
    knobs=[r.get('knob','JOB') for r in runs]
    if len(set(knobs))!=1:raise ValueError('Series differs in knob')
    knob=knobs[0];arms=[r.get('arm',r['job']) for r in runs]
    if arms!=[0,1,1,0] or [r['index'] for r in runs]!=[1,2,3,4]:raise ValueError('Need control/candidate/candidate/control order')
    if [r['job'] for r in runs]!=([0,1,1,0] if knob=='JOB' else [1]*4):raise ValueError('Option comparison must retain JOB1 in every arm')
    for field in ('scene','metric','window_seconds','exe_sha256','map_sha256','save_sha256','helper_sha256','runner_sha256','runtime_options_sha256','geometry_mask','stage_f','dump_every','cpu_binding_sha256','cpu_tool_sha256','round_intro_seconds','content_shot_at'):
        if any(r[field]!=runs[0][field] for r in runs):raise ValueError('Series differs in '+field)
    for field in ('content_method','content_capture_ms','content_tool_sha256','profile','debug_stages'):
        if any(r.get(field)!=runs[0].get(field) for r in runs):raise ValueError('Series differs in '+field)
    scene=runs[0]['scene'];metric=runs[0]['metric']
    if scene not in ('Menu','Round') or metric not in ('Fps','Cpu'):raise ValueError('Unknown series method')
    window={'Menu':[40,98],'Round':[200,270]}[scene]
    if runs[0]['window_seconds']!=window or runs[0]['dump_every'] or runs[0]['geometry_mask']!=827 or runs[0]['stage_f']!=1:raise ValueError('Wrong window/common settings')
    option_keys=set(runs[0]['job_options'])
    compact=runs[0].get('profile')=='host-groups-v1'
    if compact:
        expected_keys={'BUZZ_GEOMETRY_JOB','BUZZ_GEOMETRY_JOB_HOST_PIPELINE','BUZZ_GEOMETRY_JOB_PUBLICATION','BUZZ_GEOMETRY_JOB_COMMANDS','BUZZ_GEOMETRY_JOB_LEGACY','BUZZ_GEOMETRY_JOB_AUDIT'}
        if option_keys!=expected_keys or knob!='JOB' or runs[0].get('debug_stages')!=0:raise ValueError('Invalid compact production group inventory/knob/debug mode')
    elif runs[0].get('profile') is not None:raise ValueError('Unknown production profile')
    elif 'BUZZ_GEOMETRY_JOB_INPUT_CONTROL' not in option_keys:raise ValueError('Missing production input option')
    if knob!='JOB' and (knob not in option_keys or not re.fullmatch(r'BUZZ_GEOMETRY_(?:JOB_[A-Z0-9_]+|HOST_OUTPUT)',knob) or knob.startswith('BUZZ_GEOMETRY_JOB_AUDIT')):raise ValueError('Unknown production option')
    for run,arm in zip(runs,arms):
        if set(run['job_options'])!=option_keys:raise ValueError('Option inventory differs')
        for key,value in run['job_options'].items():
            expected=0 if key.startswith('BUZZ_GEOMETRY_JOB_AUDIT') or (compact and key=='BUZZ_GEOMETRY_JOB_LEGACY') else arm if key==knob else run['job']
            if value!=expected:raise ValueError('Mismatched job option '+key)
    if metric=='Cpu' and (not runs[0]['cpu_binding_sha256'] or not runs[0]['cpu_tool_sha256']):raise ValueError('CPU series lacks observer binding')
    if metric=='Fps' and any(runs[0][f] for f in ('cpu_binding_sha256','cpu_tool_sha256')):raise ValueError('FPS series contains CPU observer')
    if scene=='Round':
        if not scene_qa:raise ValueError('Round requires inspected content screenshots before acceptance')
        qa=json.loads(scene_qa.read_text())
        if set(qa)!=set(r['tag'] for r in runs):raise ValueError('Incomplete round scene QA')
        if any(entry.get('accepted',True) is not True for entry in qa.values()):
            raise ValueError('Round scene rejected by inspection; reject the entire series')
    summaries=[];methods=set()
    for event,run in zip(done,runs):
        tag=run['tag'];csv_path=csv_directory/f'vk-scene-{tag}.csv';log=csv_directory/f'vk-scene-{tag}.stderr.log'
        if sha(csv_path)!=event['csv_sha256'] or sha(log)!=event['log_sha256']:raise ValueError('Completed-run file hash differs')
        text=log.read_text(errors='replace')
        if compact and run['job'] and 'profile=host-groups publication=1 commands=1 ' not in text:raise ValueError('Runtime compact production profile differs')
        if any(marker not in text for marker in ('window present mode mailbox','two-context asynchronous draw batches enabled')):
            raise ValueError('Missing Vulkan mailbox/asynchronous runtime evidence')
        if any(token in text for token in ('] DIFF','SPU HALT','MISMATCH')):raise ValueError('Game reported a validation failure')
        if '[stagef] mode=1' not in text or '[geometry-native] mode=1 kernels=0x33B' not in text:raise ValueError('Runtime common settings differ')
        native_roots=max(map(int,re.findall(r'\[geometry-job/input-control/raw0\] native=(\d+) PASS',text)),default=0)
        host_pipeline=run['job_options'].get('BUZZ_GEOMETRY_JOB_HOST_PIPELINE',0)
        host_roots=max(map(int,re.findall(r'^\[geometry-job/host-pipeline\] native (\d+) \(owner count;direct host data\)$',text,re.M)),default=0)
        if run['job'] and 'BUZZ_GEOMETRY_JOB_HOST_PIPELINE' in run['job_options'] and f'host pipeline={host_pipeline} ' not in text:raise ValueError('Runtime host pipeline option differs')
        if run['job'] and ('[geometry-job] mode=1:' not in text or (host_roots if host_pipeline else native_roots)<256):raise ValueError('Missing actual native whole-job/input coverage')
        if not run['job'] and ('[geometry-job] mode=1:' in text or native_roots or host_roots):raise ValueError('Reference arm used native jobs')
        if not host_pipeline and host_roots:raise ValueError('Disabled host pipeline reported native coverage')
        if host_pipeline and native_roots:raise ValueError('Host pipeline executed legacy input producers')
        if knob=='BUZZ_GEOMETRY_JOB_FORMATTER_SPANS' and f"formatter column spans={run['job_options'][knob]} " not in text:raise ValueError('Runtime span option differs')
        registers=run['job_options'].get('BUZZ_GEOMETRY_JOB_FORMATTER_REGISTERS')
        host_output=run['job_options'].get('BUZZ_GEOMETRY_HOST_OUTPUT',0)
        output_roots=max(map(int,re.findall(r'^\[geometry-job/host-output\] native (\d+) \(owner count\)$',text,re.M)),default=0)
        if run['job'] and 'BUZZ_GEOMETRY_HOST_OUTPUT' in run['job_options'] and f'host output={host_output} ' not in text:raise ValueError('Runtime host output option differs')
        if host_output and not host_pipeline and output_roots<256:raise ValueError('Missing actual host output coverage')
        if not host_output and output_roots:raise ValueError('Disabled host output reported native coverage')
        if host_pipeline and output_roots:raise ValueError('Host pipeline executed legacy output producers')
        register_paths=[tuple(map(int,m)) for m in re.findall(r'^\[geometry-job/formatter-registers\] native=(\d+) strict-parent=(\d+) children=(\d+) strict-children=(\d+)(?: PASS)?$',text,re.M)]
        if host_pipeline and any(any(counts) for counts in register_paths):raise ValueError('Host pipeline executed legacy formatter producers')
        register_coverage=0
        if registers is not None:
            if run['job'] and f'formatter registers={registers} ' not in text:raise ValueError('Runtime register-bank option differs')
            host=register_paths
            if registers:
                if not (host_output or host_pipeline) and not any(roots>=256 and children>=256 for roots,_,children,_ in host):raise ValueError('Missing actual shared-bank formatter coverage')
                if any(parent or child for _,parent,_,child in host):raise ValueError('Shared-bank performance run contains strict comparisons')
                register_coverage=max((roots for roots,_,_,_ in host),default=0)
            elif any(roots or children for roots,_,children,_ in host):raise ValueError('Disabled register-bank path reported native coverage')
            if host_pipeline and any(roots or children for roots,_,children,_ in host):raise ValueError('Host pipeline executed legacy formatter producers')
        phases=[json.loads(line) for line in (progress.parent/f'{tag}.phases.jsonl').read_text(encoding='utf-8-sig').splitlines()]
        if not phases or phases[-1]['phase']!='end':raise ValueError('Scene did not finish')
        if scene=='Round':
            shot=csv_directory/f'vk-scene-{tag}-content.png'
            checks=[p for p in phases if p['phase']=='content-shot']
            if len(checks)!=1 or checks[0]['elapsed_ms']>=window[0]*1000:raise ValueError('Scene screenshot overlaps the measurement window')
            if not qa[tag]['inspected'] or sha(shot)!=qa[tag]['sha256']:raise ValueError('Round screenshot was not accepted')
            if run.get('content_method')=='gpu-once':
                if run.get('content_capture_ms')!=190000 or not re.fullmatch('[0-9a-f]{64}',run.get('content_tool_sha256','')):raise ValueError('Missing timed-readback settings')
                if text.count('one-shot frame dump armed delay_ms=190000 continuous_readback=0')!=1 or 'one-shot frame dump FAILED' in text or 'frame dump requires presentation readback' in text:raise ValueError('Invalid one-shot readback mode')
                captures=re.findall(r'one-shot frame dump complete frame=(\d+) elapsed_ms=([0-9.]+) continuous_readback=(\d+)',text)
                if len(captures)!=1 or captures[0][2]!='0' or not 190000<=float(captures[0][1])<195000:raise ValueError('One-shot readback did not finish before the measurement window')
                with csv_path.open(newline='') as stream:
                    captured=[row for row in csv.DictReader(stream) if int(row['frame'])==int(captures[0][0])]
                if len(captured)!=1 or not 180000<=float(captured[0]['elapsed_ms'])<195000:raise ValueError('Captured frame overlaps the measurement window or has no timing row')
                ppm=csv_directory/f'vk-scene-{tag}-content.ppm';width,height,rgb=readback.read_ppm(ppm)
                if (width,height)!=(1280,720) or not any(rgb) or shot.read_bytes()!=readback.png_bytes(width,height,rgb):raise ValueError('Scene PNG differs from native readback')
                if qa[tag].get('ppm_sha256')!=sha(ppm):raise ValueError('Native readback was not accepted')
            elif run.get('content_method') not in (None,'printwindow'):raise ValueError('Unknown round content method')
        frame=ab.summarize(csv_path,*window)
        item=dict(tag=tag,job=run['job'],arm=run.get('arm',run['job']),native_raw0_lower_bound=native_roots,frame_times=frame)
        item['native_host_pipeline_lower_bound']=host_roots
        if registers is not None:item['native_formatter_registers_lower_bound']=register_coverage
        if metric=='Cpu':
            cpu_path=progress.parent/f'{tag}.cpu.json'
            if sha(cpu_path)!=event['cpu_sha256']:raise ValueError('CPU completed-run hash differs')
            cpu=json.loads(cpu_path.read_text())
            if cpu['status']!='complete' or cpu['exe_sha256']!=run['exe_sha256'] or cpu['binding_sha256']!=run['cpu_binding_sha256'] or cpu['window_seconds']!=window:raise ValueError('CPU binding/window mismatch')
            if len(cpu['snapshots'])!=2 or any(abs(s['elapsed_ns']/1e9-t)>.05 for s,t in zip(cpu['snapshots'],window)):raise ValueError('CPU boundary missed')
            item['cpu']=work.summarize(*cpu['snapshots']);methods.add(cpu['method'])
        summaries.append(item)
    result=dict(scene=scene,metric=metric,knob=knob,window_seconds=window,exe_sha256=runs[0]['exe_sha256'],map_sha256=runs[0]['map_sha256'],save_sha256=runs[0]['save_sha256'],
        progress_sha256=sha(progress),settings=runs[0],runs=summaries,
        fps=comparison([r['frame_times']['fps'] for r in summaries]),
        limitation='One reversed series;round camera/idle poses may vary. FPS and CPU series are separate;job-boundary correctness/live/frame evidence and default decisions require separate acceptance.')
    if metric=='Cpu':
        if len(methods)!=1:raise ValueError('CPU methods differ')
        result['cpu_method']=methods.pop()
        for owner in ('pool','process'):
            result[owner]={key:comparison([r['cpu'][owner][key] for r in summaries]) for key in ('cycles_per_frame','cpu_ms_per_frame')}
    if scene=='Round':result['scene_qa_sha256']=sha(scene_qa)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('progress',type=Path);parser.add_argument('--csv-directory',type=Path,default=Path('analysis'));parser.add_argument('--scene-qa',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=report(args.progress,args.csv_directory,args.scene_qa)
    with args.output.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2);stream.write('\n')
    print(f"{result['scene']} {result['metric']} {result['knob']} FPS: {result['fps']}")
    for owner in ('pool','process'):
        if owner in result:print(owner,result[owner])


if __name__=='__main__':main()
