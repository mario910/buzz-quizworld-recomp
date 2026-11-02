"""Pinned composed original producers plus complete output conversion."""
from pathlib import Path
import runpy

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).with_name("generate-geometry-host-cull-job-test.py")))["main"](
        "host-vertex-job-v1", include_conversion=True)
