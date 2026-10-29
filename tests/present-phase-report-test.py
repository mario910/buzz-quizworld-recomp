"""Completed host-present timestamps and fixed-window phase regression."""
import importlib.util
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("phase_report", root / "tools/present_phase_report.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

result = module.report(root / "tests/fixtures/presents.trace", [0, .04])
assert result["fps"] == 100
assert result["phase_fps"] == 100
assert result["pacing"]["mean_ms"] == 10
assert result["dropped"] == 0

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "shifted.trace"
    path.write_text("# ps3_ftrace start_us=2000000 end_us=4000000 spans=4 dropped=7\n"
                    "10 0 0 0 1999990 2000010\n"
                    "10 0 0 0 2999990 3000010\n"
                    "10 0 0 0 3999990 4000010\n"
                    "9 0 0 0 3500000 3500000\n", encoding="utf-8")
    result = module.report(path, [0, 1])
    assert result["per_second"] == [1, 1]
    assert result["fps"] == result["phase_fps"] == 1
    assert result["phase_pacing"] is None
    assert result["dropped"] == 7
print("Present phase report: completed timestamps, header-relative windows, boundaries PASS")
