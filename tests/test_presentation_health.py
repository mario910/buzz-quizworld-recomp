import importlib.util
from pathlib import Path
import unittest
import struct
spec=importlib.util.spec_from_file_location('health',Path(__file__).resolve().parents[1]/'perf/observe-presentation.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def row(present,guest,flips,iconic=False,width=1280):
    return {'counters':{'g_render_perf_serial':present,'g_frames_presented':guest,'s_flip_request_count':flips},
            'windows':[{'iconic':iconic,'width':width,'height':720}]}
class HealthTest(unittest.TestCase):
    def test_session_state_and_reconnection(self):
        data=bytearray(232);struct.pack_into('<I',data,0,1)
        struct.pack_into('<IIi',data,8,3,0,1);struct.pack_into('<qq',data,176,100,50)
        session=m.parse_session_info(data,b'\x02\x00')
        self.assertEqual(session,dict(session_id=3,state=0,flags=1,protocol=2,connected_100ns=100,disconnected_100ns=50))
        def samples():return [dict(elapsed_ms=t,session=dict(session)) for t in range(45000,90000,500)]
        def signature(rows):return m.session_window_signature(rows,45000,90000)
        self.assertIsNotNone(signature(samples()))
        for field,value in [('state',4),('flags',0),('protocol',0),('connected_100ns',101),('disconnected_100ns',51)]:
            rows=samples();rows[30]['session'][field]=value;self.assertIsNone(signature(rows))
        self.assertIsNone(signature([]))
        rows=samples();rows[30].pop('session');self.assertIsNone(signature(rows))
        with self.assertRaises(ValueError):m.parse_session_info(data[:20],b'\x02\x00')
    def test_cpu_load_separates_system_and_game(self):
        a=dict(time_ns=100,system_total_ns=200,system_idle_ns=80,game_cpu_ns=50,game_cycles=100,processor_groups=1)
        b=dict(time_ns=110,system_total_ns=240,system_idle_ns=90,game_cpu_ns=70,game_cycles=160,processor_groups=1)
        self.assertEqual(m.cpu_load_delta(a,b),dict(system_busy_cores=3,game_busy_cores=2,other_busy_cores_estimate=1,game_cycles_per_cpu_ns=3))
        self.assertIsNone(m.cpu_load_delta(a,dict(b,processor_groups=2)))
        with self.assertRaises(ValueError):m.cpu_load_delta(b,a)
        with self.assertRaises(ValueError):m.cpu_load_delta(a,a)
    def test_progress_and_minimize(self):
        a=row(10,10,10)
        self.assertEqual(m.classify(a,row(11,11,11)),'presenting')
        self.assertEqual(m.classify(a,row(10,20,20,True)),'window_unavailable')
        self.assertEqual(m.classify(a,row(10,20,20,width=0)),'window_unavailable')
    def test_do_not_call_every_failure_a_queue_deadlock(self):
        a=row(10,10,10)
        self.assertEqual(m.classify(a,row(10,20,20)),'guest_present_calls_without_window_present')
        self.assertEqual(m.classify(a,row(10,10,20)),'guest_flips_without_present_calls')
        self.assertEqual(m.classify(a,a),'no_progress_observed')
    def test_measurement_window_coverage_and_visibility(self):
        def samples():return [dict(row(1,1,1),elapsed_ms=t) for t in range(45000,90000,500)]
        def valid(rows):return m.validate_window(rows,45000,90000,1280,720)
        self.assertTrue(valid(samples()))
        self.assertFalse(valid([]))
        self.assertFalse(valid(samples()[4:]))
        self.assertFalse(valid(samples()[:-4]))
        data=samples();del data[25:30];self.assertFalse(valid(data))
        data=samples();data[25]['windows'][0]['iconic']=True;self.assertFalse(valid(data))
        data=samples();data[25]['windows'][0]['width']=640;self.assertFalse(valid(data))
        data=samples();data[25]['windows']=[];self.assertFalse(valid(data))
        data=samples();data[25]['elapsed_ms']=data[24]['elapsed_ms'];self.assertFalse(valid(data))
