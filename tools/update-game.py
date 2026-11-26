"""Update the game in dist/ using the selected Buzz Prepare build sources."""
import argparse
from pathlib import Path
import sys
import subprocess
import threading

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root))
from launcher.game_update import update_game

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4, choices=range(1, 65), metavar="1-64")
    parser.add_argument("--bundled-only", action="store_true", help="Upgrade from the installed stable package without checking GitHub.")
    args = parser.parse_args()
    try:
        update_game(root, root / "dist", args.jobs, lambda line: print(line, flush=True), threading.Event(),
                    bundled_only=args.bundled_only)
    except subprocess.CalledProcessError as exc:
        sys.exit("Game update stopped: " + (exc.stderr or str(exc)).strip())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit(f"Game update stopped: {exc}")
