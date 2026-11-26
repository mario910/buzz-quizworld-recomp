"""Command-line helpers of the preparation pipeline.

BuzzSetup (preparation/) is the user interface for preparing and starting the
game; the game itself has the PC settings menu.
"""
import argparse
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description='Buzz! Quiz World preparation helpers')
parser.add_argument('--install-upscaler', action='store_true',
                    help='Download (or, with --archive, unpack) the Real-ESRGAN texture upscaler into RUNTIME/textures/upscaler')
parser.add_argument('--runtime', type=Path, help="Game runtime folder for --install-upscaler (default: this checkout's runtime)")
parser.add_argument('--archive', type=Path, help='Local copy of the pinned upscaler release for --install-upscaler')
args = parser.parse_args()
if not args.install_upscaler:
    parser.print_help()
    sys.exit(2)
from . import upscaler_install
project = Path(__file__).resolve().parents[1]
runtime = (args.runtime or project / 'runtime').resolve()
try:
    upscaler_install.install(runtime, print, archive=args.archive, cache=project / 'runtime/setup/downloads')
except (ValueError, OSError) as exc:
    print(f'Upscaler not installed: {exc}', file=sys.stderr)
    sys.exit(1)
