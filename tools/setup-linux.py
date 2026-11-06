"""Install the pinned preparation packages into this checkout's Linux venv."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
env = root / "toolenv"
if sys.platform != "linux" or sys.version_info < (3, 10):
    sys.exit("Linux and Python 3.10+ are required.")
subprocess.run([sys.executable, "-m", "venv", env], check=True)
subprocess.run([env / "bin/python", "-m", "pip", "install", "-r", root / "requirements.txt"], check=True)
