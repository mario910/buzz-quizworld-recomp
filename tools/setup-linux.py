"""Install the pinned preparation packages into this checkout's POSIX venv."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
env = Path(os.environ.get("BUZZ_TOOL_ENV") or root / "toolenv")
if sys.platform not in {"linux", "darwin"} or sys.version_info < (3, 10):
    sys.exit("Linux or macOS and Python 3.10+ are required.")
subprocess.run([sys.executable, "-m", "venv", env], check=True)
subprocess.run([env / "bin/python", "-m", "pip", "install", "-r", root / "requirements.txt"], check=True)
