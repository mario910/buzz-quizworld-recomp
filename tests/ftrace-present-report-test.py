"""The report must not confuse guest flip requests with host presentations."""
import subprocess
import sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
command=[sys.executable,str(root/'ps3recomp/tools/ftrace_report.py'),str(root/'tests/fixtures/presents.trace'),'--frames','0']
for source,expected in [('flip','6 frames (flip), median 5.0 ms'),('present','3 frames (present), median 10.0 ms')]:
    result=subprocess.run(command+['--frame-source',source],capture_output=True,text=True,check=True)
    assert expected in result.stdout,result.stdout
    assert 'Host presents: 4, interval mean 10.00 ms (100.00 FPS)' in result.stdout,result.stdout
print('PASS: independent guest flip and completed host present intervals')
