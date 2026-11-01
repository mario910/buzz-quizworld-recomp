import copy
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('job_report',Path(__file__).resolve().parents[1]/'perf/geometry-job-report.py')
reporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(reporter)


class JobReportTests(unittest.TestCase):
    def save(self,path,events):path.write_text('\n'.join(json.dumps(e) for e in events))

    def fixture(self,root):
        events=[]
        for index,job in enumerate((0,1,1,0),1):
            tag=f'job-{index}';csv_path=root/f'vk-scene-{tag}.csv';log=root/f'vk-scene-{tag}.stderr.log'
            log.write_text('[stagef] mode=1\n[geometry-native] mode=1 kernels=0x33B\n'+('[geometry-job] mode=1: native\n[geometry-job/input-control/raw0] native=256 PASS\n' if job else ''))
            with csv_path.open('w',newline='') as stream:
                writer=csv.writer(stream);writer.writerow(('elapsed_ms','interval_ms','drain_ms'))
                for t in range(0,100001,400 if job else 500):writer.writerow((t,400 if job else 500,20))
            (root/f'{tag}.phases.jsonl').write_text(json.dumps(dict(phase='end',elapsed_ms=100000)))
            run=dict(tag=tag,scene='Menu',metric='Fps',job=job,index=index,window_seconds=[40,98],exe_sha256='a'*64,map_sha256='b'*64,save_sha256='c'*64,
                helper_sha256='d'*64,runner_sha256='e'*64,runtime_options_sha256='f'*64,geometry_mask=827,stage_f=1,dump_every=0,cpu_binding_sha256='',cpu_tool_sha256='',round_intro_seconds=50,content_shot_at=195,
                job_options={'BUZZ_GEOMETRY_JOB':job,'BUZZ_GEOMETRY_JOB_INPUT_CONTROL':job,'BUZZ_GEOMETRY_JOB_AUDIT':0})
            events.extend((dict(state='start',run=run),dict(state='done',run=run,csv_sha256=reporter.sha(csv_path),log_sha256=reporter.sha(log),cpu_sha256='')))
        path=root/'progress.jsonl';self.save(path,events);return path,events

    def test_reversed_fps_result(self):
        with tempfile.TemporaryDirectory() as temp:
            path,_=self.fixture(Path(temp));result=reporter.report(path,Path(temp))
            self.assertAlmostEqual(result['fps']['change_percent'],25)
            self.assertEqual([round(v) for v in result['fps']['pair_changes_percent']],[25,25])

    def test_incomplete_or_mixed_series_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path,events=self.fixture(Path(temp));self.save(path,events[:-1])
            with self.assertRaises(ValueError):reporter.report(path,Path(temp))
            for key,value in (('job',0),('exe_sha256','x'*64),('save_sha256','x'*64),('helper_sha256','x'*64),('dump_every',500)):
                altered=copy.deepcopy(events);altered[2]['run'][key]=value;altered[3]['run'][key]=value;self.save(path,altered)
                with self.subTest(key=key),self.assertRaises(ValueError):reporter.report(path,Path(temp))

    def test_changed_csv_or_option_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,events=self.fixture(root);(root/'vk-scene-job-1.csv').write_text('changed')
            with self.assertRaisesRegex(ValueError,'file hash'):reporter.report(path,root)
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,events=self.fixture(root);events[2]['run']['job_options']['BUZZ_GEOMETRY_JOB_INPUT_CONTROL']=0;self.save(path,events)
            with self.assertRaisesRegex(ValueError,'job option'):reporter.report(path,root)

    def test_round_cannot_pass_without_inspected_scene(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,events=self.fixture(root)
            for event in events:event['run'].update(scene='Round',window_seconds=[200,270])
            self.save(path,events)
            with self.assertRaisesRegex(ValueError,'inspected content'):reporter.report(path,root)

    def test_empty_native_coverage_cannot_be_a_native_measurement(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,events=self.fixture(root);log=root/'vk-scene-job-2.stderr.log'
            log.write_text('[stagef] mode=1\n[geometry-native] mode=1 kernels=0x33B\n[geometry-job] mode=1: native\n')
            events[3]['log_sha256']=reporter.sha(log);self.save(path,events)
            with self.assertRaisesRegex(ValueError,'actual native'):reporter.report(path,root)

    def cpu_fixture(self,root):
        path,events=self.fixture(root)
        for start,done in zip(events[::2],events[1::2]):
            run=start['run'];run.update(metric='Cpu',cpu_binding_sha256='b'*64,cpu_tool_sha256='d'*64)
            before=dict(elapsed_ns=40_000_000_000,frames_before=1000,frames_after=1000,process=dict(created_100ns=1,cycles=100,cpu_ns=100),threads=[])
            after=copy.deepcopy(before);after.update(elapsed_ns=98_000_000_000,frames_before=3000,frames_after=3000)
            cost=900000 if run['job'] else 1000000
            after['process'].update(cycles=100+cost*8,cpu_ns=100+cost*16)
            for i in range(1,5):
                lane=dict(tid=i,created_100ns=i+1,name=f'SPURS pool lane {i}',cycles=100,cpu_ns=100)
                before['threads'].append(lane);after['threads'].append(dict(lane,cycles=100+cost,cpu_ns=100+cost*2))
            cpu=dict(status='complete',exe_sha256=run['exe_sha256'],binding_sha256=run['cpu_binding_sha256'],method='synthetic',window_seconds=[40,98],snapshots=[before,after])
            cpu_path=root/f"{run['tag']}.cpu.json";cpu_path.write_text(json.dumps(cpu));done['cpu_sha256']=reporter.sha(cpu_path)
        self.save(path,events);return path,events

    def test_cpu_uses_completed_frames_and_raw_counters(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_=self.cpu_fixture(root);result=reporter.report(path,root)
            for owner in ('pool','process'):
                for values in result[owner].values():self.assertAlmostEqual(values['change_percent'],-10)

    def test_cached_cpu_summary_cannot_hide_reset(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,events=self.cpu_fixture(root);cpu_path=root/'job-1.cpu.json';cpu=json.loads(cpu_path.read_text())
            cpu['snapshots'][1]['process']['cycles']=0;cpu['process']=dict(cycles_per_frame=1);cpu_path.write_text(json.dumps(cpu))
            events[1]['cpu_sha256']=reporter.sha(cpu_path);self.save(path,events)
            with self.assertRaisesRegex(ValueError,'counter reset'):reporter.report(path,root)


if __name__=='__main__':unittest.main()
