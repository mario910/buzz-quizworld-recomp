import importlib.util
import random
from pathlib import Path
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("dependencies", root / "tools/task_dependency_report.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
rng = random.Random(1739)
for _ in range(3000):
    spans = [sorted(rng.sample(range(50), 2)) for _ in range(rng.randrange(20))]
    coverage = module.Coverage(spans)
    lo, hi = sorted(rng.sample(range(51), 2))
    expected = sum(any(a <= t < b for a,b in spans) for t in range(lo,hi))
    assert coverage.measure(lo, hi) == expected

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "fixture.trace"
    path.write_text("# fixture\n10 0 0 0 1 10\n10 0 0 0 21 30\n"
                    "16 2 29 100 11 20\n1 1 55 0 11 25\n", encoding="utf-8")
    _, records, presents = module.read(path)
    assert presents == [10,30]  # Completion times, NOT 1 and 21.
    assert len(records) == 2
    path.write_text("# ps3_ftrace start_us=10 end_us=30 spans=3 dropped=0\n"
                    "10 0 0 0 1 9\n10 0 0 0 1 10\n10 0 0 0 21 30\n", encoding="utf-8")
    assert module.read(path)[2] == [10]  # Do not include incomplete/outside capture frames.
print("PASS dependency report: 3000 interval-oracle cases, completed-present timestamps")
