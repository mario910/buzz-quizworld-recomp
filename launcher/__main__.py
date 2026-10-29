import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description='Buzz! Quiz World PC launcher')
parser.add_argument('--smoke', action='store_true', help='Construct a withdrawn UI; no game/process launch')
parser.add_argument('--prepare', action='store_true', help='Open the ISO/firmware preparation wizard')
parser.add_argument('--project', type=Path, help='Use a prepared, isolated build workspace')
args = parser.parse_args()
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
