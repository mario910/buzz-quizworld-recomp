"""Unattended entry point to the same private pipeline as the setup wizard."""
import argparse
from pathlib import Path
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from launcher.setup_pipeline import Inputs, prepare

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--game", required=True)
parser.add_argument("--firmware", required=True)
parser.add_argument("--keys", default="", help="Private JSON/scetool key file; required for encrypted SELF/PUP inputs")
parser.add_argument("--update", default="")
parser.add_argument("--jobs", type=int, default=6)
parser.add_argument("--upscaler", action="store_true", help="Install the optional Real-ESRGAN texture tool")
parser.add_argument("--resume", type=Path)
parser.add_argument("--cancel-file", type=Path, help=argparse.SUPPRESS)
mode = parser.add_mutually_exclusive_group()
mode.add_argument("--import-only", action="store_true")
mode.add_argument("--dist", action="store_true", help="Build and publish the standalone Windows distribution")
parser.add_argument("--accept-rebuilt", action="store_true", help="Explicitly verify/register a rebuilt EXE against its build manifest")
args = parser.parse_args()
cancel = threading.Event()
if args.cancel_file:
    def watch_cancel():
        while not cancel.wait(0.25):
            if args.cancel_file.exists():
                cancel.set()
    threading.Thread(target=watch_cancel, daemon=True).start()
try:
    session, state = prepare(Inputs(args.game, args.firmware, args.keys, args.update, args.jobs, args.upscaler),
                             Path(__file__).resolve().parents[1], lambda message: print(message, flush=True),
                             cancel, resume=args.resume, build=not args.import_only, accept_rebuilt=args.accept_rebuilt, standalone=args.dist)
    print("SESSION=" + str(session))
    print("PROFILE=" + state["profile"]["key"])
except (ValueError, OSError, RuntimeError) as exc:
    print("Preparation stopped: " + str(exc), file=sys.stderr)
    sys.exit(1)
