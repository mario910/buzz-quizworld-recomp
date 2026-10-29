import io
import runpy
from pathlib import Path

inspect = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'inspect-frame-boundaries.py'))['inspect']
trace = '# fixture\nT 1 main\n' + '\n'.join([
    '9 0 0 1 1 1', '8 0 0 0 2 2', '25 1 0 ffff 2 2',
    '25 4 ffffffff 100 3 3', '10 0 0 0 3 4', '25 9 ffffffff 0 5 5',
    '9 0 1 1 6 6', '8 0 1 0 7 7', '25 1 1 10000 7 7',
    '25 4 0 20 8 8', '10 0 1 0 8 9', '25 8 0 2 10 11',
    '25 6 1 3 12 12', '25 7 1 3 13 13', '25 3 0 ffff 14 14'])
result = inspect(io.StringIO(trace))
assert result['counts']['host_presents'] == 2
assert result['counts']['fifo_flips'] == 2
assert result['counts']['flip_requests'] == 2
assert result['counts']['empty_batch'] == 1
assert result['counts']['offscreen_execution'] == 1
assert result['counts']['catchup_skip'] == 1
assert result['counts']['small_batch_hold'] == 1
assert result['counts']['ticker_suppressed'] == 1
assert result['host_decision_serial_gaps'] == {1: 1}  # uint32 wrap, not a lost frame
assert result['host_decision_draw_range'] == [32, 256]
assert inspect(io.StringIO(''))['host_decision_draw_range'] == [None, 0]
print('PASS: distinct FIFO, request, gate and actual-present counts; uint32 serial wrap')
