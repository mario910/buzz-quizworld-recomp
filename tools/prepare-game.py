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
parser.add_argument("--helper", required=True)
parser.add_argument("--update", default="")
parser.add_argument("--jobs", type=int, default=6)
parser.add_argument("--resume", type=Path)
parser.add_argument("--import-only", action="store_true")
parser.add_argument("--accept-rebuilt", action="store_true", help="Explicitly verify/register a rebuilt EXE against its build manifest")
args = parser.parse_args()
try:
    session, state = prepare(Inputs(args.game, args.firmware, args.helper, args.update, args.jobs),
                             Path(__file__).resolve().parents[1], lambda message: print(message, flush=True),
                             threading.Event(), resume=args.resume, build=not args.import_only, accept_rebuilt=args.accept_rebuilt)
    print("SESSION=" + str(session))
    print("PROFILE=" + state["profile"]["key"])
except (ValueError, OSError, RuntimeError) as exc:
    print("Preparation stopped: " + str(exc), file=sys.stderr)
    sys.exit(1)
