import argparse
from .gui import run

parser = argparse.ArgumentParser(description='Buzz! Quiz World PC launcher')
parser.add_argument('--smoke', action='store_true', help='Construct a withdrawn UI; no game/process launch')
args = parser.parse_args()
run(smoke=args.smoke)
