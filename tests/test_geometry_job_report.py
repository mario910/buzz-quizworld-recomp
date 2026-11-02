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
            log.write_text('window present mode mailbox\ntwo-context asynchronous draw batches enabled\n[stagef] mode=1\n[geometry-native] mode=1 kernels=0x33B\n'+('[geometry-job] mode=1: native\n[geometry-job/input-control/raw0] native=256 PASS\n' if job else ''))
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

    def test_missing_vulkan_runtime_markers_rejected(self):
        for marker in ('window present mode mailbox','two-context asynchronous draw batches enabled'):
            with self.subTest(marker=marker),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);path,events=self.fixture(root);log=root/'vk-scene-job-2.stderr.log'
                log.write_text(log.read_text().replace(marker,''))
                events[3]['log_sha256']=reporter.sha(log);self.save(path,events)
                with self.assertRaisesRegex(ValueError,'Vulkan mailbox/asynchronous'):
                    reporter.report(path,root)

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
            log.write_text('window present mode mailbox\ntwo-context asynchronous draw batches enabled\n[stagef] mode=1\n[geometry-native] mode=1 kernels=0x33B\n[geometry-job] mode=1: native\n')
            events[3]['log_sha256']=reporter.sha(log);self.save(path,events)
            with self.assertRaisesRegex(ValueError,'actual native'):reporter.report(path,root)

    def compact_fixture(self,root):
        path,events=self.fixture(root)
        for start,done in zip(events[::2],events[1::2]):
            run=start['run'];job=run['job'];run.update(profile='host-groups-v1',debug_stages=0)
            run['job_options']={key:job for key in ('BUZZ_GEOMETRY_JOB','BUZZ_GEOMETRY_JOB_HOST_PIPELINE','BUZZ_GEOMETRY_JOB_PUBLICATION','BUZZ_GEOMETRY_JOB_COMMANDS')}
            run['job_options'].update(BUZZ_GEOMETRY_JOB_LEGACY=0,BUZZ_GEOMETRY_JOB_AUDIT=0)
            log=root/f"vk-scene-{run['tag']}.stderr.log"
            text=log.read_text().replace('[geometry-job/input-control/raw0] native=256 PASS\n','')
            if job:text+='[geometry-job] profile=host-groups publication=1 commands=1 (JOB default off)\n[geometry-job] host pipeline=1 (host SoA)\n[geometry-job/host-pipeline] native 25000 (owner count;direct host data)\n'
            log.write_text(text);done['log_sha256']=reporter.sha(log)
        self.save(path,events);return path,events

    def test_compact_groups_require_native_host_coverage_without_legacy_options(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_=self.compact_fixture(root);result=reporter.report(path,root)
            self.assertEqual(result['settings']['profile'],'host-groups-v1')
            self.assertEqual([r['native_host_pipeline_lower_bound'] for r in result['runs']],[0,25000,25000,0])
            self.assertEqual([r['native_raw0_lower_bound'] for r in result['runs']],[0,0,0,0])
            self.assertAlmostEqual(result['fps']['change_percent'],25)

    def test_compact_groups_reject_mixed_or_legacy_profile_and_false_coverage(self):
        for fault in ('extra-leaf','legacy-on','group-off','mixed-profile','debug-stages','runtime-profile','missing-native','legacy-input','legacy-formatter'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);path,events=self.compact_fixture(root);run=events[2]['run']
                log=root/'vk-scene-job-2.stderr.log';text=log.read_text()
                if fault=='extra-leaf':run['job_options']['BUZZ_GEOMETRY_JOB_INPUT_CONTROL']=1
                elif fault=='legacy-on':run['job_options']['BUZZ_GEOMETRY_JOB_LEGACY']=1
                elif fault=='group-off':run['job_options']['BUZZ_GEOMETRY_JOB_PUBLICATION']=0
                elif fault=='mixed-profile':run['profile']='legacy'
                elif fault=='debug-stages':run['debug_stages']=1
                elif fault=='runtime-profile':text=text.replace('profile=host-groups','profile=legacy-debug')
                elif fault=='missing-native':text=text.replace('native 25000','native 255')
                elif fault=='legacy-input':text+='[geometry-job/input-control/raw0] native=256 PASS\n'
                else:text+='[geometry-job/formatter-registers] native=256 strict-parent=0 children=512 strict-children=0 PASS\n'
                log.write_text(text);events[3]['log_sha256']=reporter.sha(log);self.save(path,events)
                with self.assertRaises(ValueError):reporter.report(path,root)

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

    def option_fixture(self,root):
        path,events=self.fixture(root)
        for start,done in zip(events[::2],events[1::2]):
            run=start['run'];arm=run['job'];run.update(job=1,arm=arm,knob='BUZZ_GEOMETRY_JOB_FORMATTER_SPANS')
            run['job_options'].update(BUZZ_GEOMETRY_JOB=1,BUZZ_GEOMETRY_JOB_INPUT_CONTROL=1,BUZZ_GEOMETRY_JOB_FORMATTER_SPANS=arm)
            log=root/f"vk-scene-{run['tag']}.stderr.log"
            log.write_text('window present mode mailbox\ntwo-context asynchronous draw batches enabled\n[stagef] mode=1\n[geometry-native] mode=1 kernels=0x33B\n[geometry-job] mode=1: native\n[geometry-job/input-control/raw0] native=256 PASS\n'+f'[geometry-job] formatter column spans={arm} (0 exhaustive,1 proven spans)\n')
            done['log_sha256']=reporter.sha(log)
        self.save(path,events);return path,events

    def test_single_option_keeps_job_native_in_every_arm(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_=self.option_fixture(root);result=reporter.report(path,root)
            self.assertEqual(result['knob'],'BUZZ_GEOMETRY_JOB_FORMATTER_SPANS')
            self.assertEqual([r['job'] for r in result['runs']],[1]*4)
            self.assertEqual([r['arm'] for r in result['runs']],[0,1,1,0])
            self.assertAlmostEqual(result['fps']['change_percent'],25)

    def register_fixture(self,root):
        path,events=self.option_fixture(root)
        for start,done in zip(events[::2],events[1::2]):
            run=start['run'];arm=run['arm'];run['knob']='BUZZ_GEOMETRY_JOB_FORMATTER_REGISTERS'
            run['job_options'].update(BUZZ_GEOMETRY_JOB_FORMATTER_SPANS=1,BUZZ_GEOMETRY_JOB_FORMATTER_REGISTERS=arm)
            log=root/f"vk-scene-{run['tag']}.stderr.log"
            text=log.read_text().replace(f'formatter column spans={arm} ','formatter column spans=1 ')+f'[geometry-job] formatter registers={arm} (0 ABI,1 shared bank)\n'
            if arm:text+='[geometry-job/formatter-registers] native=256 strict-parent=0 children=512 strict-children=0 PASS\n'
            log.write_text(text);done['log_sha256']=reporter.sha(log)
        self.save(path,events);return path,events

    def test_shared_register_option_requires_actual_unchecked_coverage(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_=self.register_fixture(root);result=reporter.report(path,root)
            self.assertEqual(result['knob'],'BUZZ_GEOMETRY_JOB_FORMATTER_REGISTERS')
            self.assertEqual([r['native_formatter_registers_lower_bound'] for r in result['runs']],[0,256,256,0])
            self.assertAlmostEqual(result['fps']['change_percent'],25)

    def host_output_fixture(self,root):
        path,events=self.fixture(root)
        for start,done in zip(events[::2],events[1::2]):
            run=start['run'];job=run['job'];run['job_options'].update(BUZZ_GEOMETRY_HOST_OUTPUT=job,BUZZ_GEOMETRY_JOB_FORMATTER_REGISTERS=job)
            log=root/f"vk-scene-{run['tag']}.stderr.log"
            text=log.read_text()+f'[geometry-job] formatter registers={job} (shared bank)\n[geometry-job] host output={job} (SoA)\n'
            if job:text+='[geometry-job/host-output] native 25000 (owner count)\n'
            log.write_text(text);done['log_sha256']=reporter.sha(log)
        self.save(path,events);return path,events

    def test_host_output_replaces_formatter_coverage(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_=self.host_output_fixture(root)
            result=reporter.report(path,root)
            self.assertEqual([r['native_formatter_registers_lower_bound'] for r in result['runs']],[0,0,0,0])

    def test_host_output_requires_actual_coverage_and_correct_flag(self):
        for fault in ('missing','getter','disabled','strict'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);path,events=self.host_output_fixture(root)
                index=0 if fault=='disabled' else 1
                done=events[index*2+1];log=root/f"vk-scene-{done['run']['tag']}.stderr.log";text=log.read_text()
                if fault=='missing':text=text.replace('native 25000 (owner count)','native 255 (owner count)')
                elif fault=='getter':text=text.replace('host output=1','host output=0')
                elif fault=='disabled':text+='[geometry-job/host-output] native 256 (owner count)\n'
                else:text+='[geometry-job/formatter-registers] native=256 strict-parent=1 children=512 strict-children=0 PASS\n'
                log.write_text(text);done['log_sha256']=reporter.sha(log);self.save(path,events)
                with self.assertRaises(ValueError):reporter.report(path,root)

    def test_shared_register_option_rejects_missing_false_or_checked_coverage(self):
        for fault in ('getter','missing','roots','children','parent-strict','child-strict','off-active'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);path,events=self.register_fixture(root)
                index=0 if fault=='off-active' else 1
                event=events[2*index+1];log=root/f"vk-scene-{event['run']['tag']}.stderr.log";text=log.read_text()
                if fault=='getter':text=text.replace('formatter registers=1','formatter registers=0')
                elif fault=='missing':text='\n'.join(line for line in text.splitlines() if '/formatter-registers]' not in line)+'\n'
                elif fault=='roots':text=text.replace('native=256 strict-parent','native=255 strict-parent')
                elif fault=='children':text=text.replace('children=512','children=255')
                elif fault=='parent-strict':text=text.replace('strict-parent=0','strict-parent=1')
                elif fault=='child-strict':text=text.replace('strict-children=0','strict-children=1')
                else:text+='[geometry-job/formatter-registers] native=256 strict-parent=0 children=512 strict-children=0 PASS\n'
                log.write_text(text);event['log_sha256']=reporter.sha(log);self.save(path,events)
                with self.assertRaises(ValueError):reporter.report(path,root)

    def test_option_series_rejects_other_flags_or_job_changes(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,events=self.option_fixture(root)
            for change in ('job','common','knob'):
                altered=copy.deepcopy(events)
                if change=='job':altered[2]['run']['job']=altered[3]['run']['job']=0
                elif change=='common':altered[2]['run']['job_options']['BUZZ_GEOMETRY_JOB_INPUT_CONTROL']=0;altered[3]['run']['job_options']['BUZZ_GEOMETRY_JOB_INPUT_CONTROL']=0
                else:altered[2]['run']['knob']=altered[3]['run']['knob']='JOB'
                self.save(path,altered)
                with self.subTest(change=change),self.assertRaises(ValueError):reporter.report(path,root)

    def test_option_series_checks_actual_runtime_setting(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,events=self.option_fixture(root);log=root/'vk-scene-job-2.stderr.log'
            log.write_text(log.read_text().replace('spans=1','spans=0'));events[3]['log_sha256']=reporter.sha(log);self.save(path,events)
            with self.assertRaisesRegex(ValueError,'Runtime span'):reporter.report(path,root)

    def gpu_round_fixture(self,root):
        path,events=self.option_fixture(root);qa={}
        for start,done in zip(events[::2],events[1::2]):
            run=start['run'];tag=run['tag']
            run.update(scene='Round',window_seconds=[200,270],content_method='gpu-once',content_capture_ms=190000,content_tool_sha256='f'*64)
            csv_path=root/f'vk-scene-{tag}.csv'
            with csv_path.open('w',newline='') as stream:
                writer=csv.writer(stream);writer.writerow(('frame','elapsed_ms','interval_ms','drain_ms'))
                for i in range(751):writer.writerow((i,i*400,400,20))
            log=root/f'vk-scene-{tag}.stderr.log'
            log.write_text(log.read_text()+'[RSX vulkan] one-shot frame dump armed delay_ms=190000 continuous_readback=0\n[RSX vulkan] one-shot frame dump complete frame=475 elapsed_ms=190010.000 continuous_readback=0\n')
            done.update(csv_sha256=reporter.sha(csv_path),log_sha256=reporter.sha(log))
            (root/f'{tag}.phases.jsonl').write_text('\n'.join(json.dumps(p) for p in (dict(phase='content-shot',elapsed_ms=195100),dict(phase='end',elapsed_ms=300000))))
            ppm=root/f'vk-scene-{tag}-content.ppm';png=root/f'vk-scene-{tag}-content.png'
            rgb=b'\x12\x34\x56'*(1280*720);ppm.write_bytes(b'P6\n1280 720\n255\n'+rgb);png.write_bytes(reporter.readback.png_bytes(1280,720,rgb))
            qa[tag]=dict(inspected=True,sha256=reporter.sha(png),ppm_sha256=reporter.sha(ppm))
        self.save(path,events);qa_path=root/'qa.json';qa_path.write_text(json.dumps(qa));return path,events,qa_path

    def test_round_requires_one_native_frame_before_timing_and_exact_pixels(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_,qa=self.gpu_round_fixture(root)
            self.assertEqual(reporter.report(path,root,qa)['scene'],'Round')

    def test_inspected_rejected_scene_invalidates_the_whole_complete_series(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_,qa_path=self.gpu_round_fixture(root)
            self.assertEqual(reporter.report(path,root,qa_path)['scene'],'Round')
            qa=json.loads(qa_path.read_text());qa['job-2']['accepted']=False
            qa_path.write_text(json.dumps(qa))
            with self.assertRaisesRegex(ValueError,'reject the entire series'):
                reporter.report(path,root,qa_path)

    def test_round_rejects_late_repeated_or_continuous_readbacks(self):
        for change in ('late','repeated','continuous','wrong-frame'):
            with self.subTest(change=change),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);path,events,qa=self.gpu_round_fixture(root);log=root/'vk-scene-job-2.stderr.log';text=log.read_text()
                if change=='late':text=text.replace('elapsed_ms=190010.000','elapsed_ms=200010.000')
                elif change=='repeated':text+=text.splitlines()[-1]+'\n'
                elif change=='continuous':text=text.replace('continuous_readback=0','continuous_readback=1')
                else:text=text.replace('frame=475','frame=510')
                log.write_text(text);events[3]['log_sha256']=reporter.sha(log);self.save(path,events)
                with self.assertRaises(ValueError):reporter.report(path,root,qa)

    def test_round_rejects_png_that_is_not_the_native_rgb_readback(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_,qa=self.gpu_round_fixture(root);ppm=root/'vk-scene-job-2-content.ppm';ppm.write_bytes(ppm.read_bytes()[:-1]+b'\x00')
            with self.assertRaisesRegex(ValueError,'PNG differs'):reporter.report(path,root,qa)


if __name__=='__main__':unittest.main()
