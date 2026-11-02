import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description='Buzz! Quiz World PC launcher')
parser.add_argument('--smoke', action='store_true', help='Construct a withdrawn UI; no game/process launch')
parser.add_argument('--prepare', action='store_true', help='Open the ISO/firmware preparation wizard')
parser.add_argument('--project', type=Path, help='Use a prepared, isolated build workspace')
parser.add_argument('--install-upscaler', action='store_true',
                    help='Download (or, with --archive, unpack) the Real-ESRGAN texture upscaler into RUNTIME/textures/upscaler')
parser.add_argument('--runtime', type=Path, help='Game runtime folder for --install-upscaler (default: this checkout\'s runtime)')
parser.add_argument('--archive', type=Path, help='Local copy of the pinned upscaler release for --install-upscaler')
args = parser.parse_args()
if args.install_upscaler:
    import sys
    from . import upscaler_install
    project = Path(__file__).resolve().parents[1]
    runtime = (args.runtime or project / 'runtime').resolve()
    try:
        upscaler_install.install(runtime, print, archive=args.archive, cache=project / 'runtime/setup/downloads')
    except (ValueError, OSError) as exc:
        print(f'Upscaler not installed: {exc}', file=sys.stderr)
        sys.exit(1)
    sys.exit(0)
if not args.prepare and not args.project and not args.smoke:
    from .active_build import ready_workspace
    project = Path(__file__).resolve().parents[1]
    args.project = ready_workspace(project)
    if args.project is None and not (project / 'build-windows/BuzzQuizWorldRecomp.exe').is_file():
        args.prepare = True
if args.prepare:
    from .setup_gui import run
else:
    from .gui import run
options = dict(smoke=args.smoke)
if args.project:
    options['project'] = args.project.resolve()
run(**options)
