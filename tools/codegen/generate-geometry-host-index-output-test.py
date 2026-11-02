"""Reuse the pinned composed graph with a separate, immutable evidence path."""
from pathlib import Path
import runpy

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).with_name("generate-geometry-host-cull-job-test.py")))["main"]("host-index-output-v1")
