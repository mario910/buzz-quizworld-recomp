"""Refresh/check source-derived configuration tables; does not run CMake or a game."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
START = '<!-- generated-options:start -->'
END = '<!-- generated-options:end -->'


def tables():
    lines = ['## Build switches (source defaults)', '',
             'Defaults below are declarations, not an existing CMake cache. Values in `${...}`',
             'are conditional; the source link contains the admission/build condition.',
             'A cached `ON` survives reconfiguration unless explicitly reset.', '',
             '| Switch | Source default | Meaning / defining source |',
             '|---|---|---|']
    for path in [ROOT/'port/CMakeLists.txt', *sorted((ROOT/'cmake').glob('*.cmake'))]:
        text = path.read_text(encoding='utf-8')
        for name, description, default in re.findall(
                r'option\(\s*((?:BUZZ|PORT)_\w+)\s+"([^"]*)"\s+([^\n)]*)\)', text):
            source = path.relative_to(ROOT).as_posix()
            lines.append(f'| `{name}` | `{default.strip()}` | {description} ([source](../{source})) |')
        for name, default, description in re.findall(
                r'set\(\s*((?:BUZZ|PORT)_\w+)\s+"([^"]*)"\s+CACHE\s+STRING\s+"([^"]*)"\)', text):
            source = path.relative_to(ROOT).as_posix()
            lines.append(f'| `{name}` | `{default or "empty"}` | {description} ([source](../{source})) |')
    profile = {}
    for line in (ROOT/'android/profiles/reference29.properties').read_text().splitlines():
        if line and not line.startswith('#'):
            key, value = line.split('=', 1)
            if key in profile:
                raise ValueError('Duplicate profile property: '+key)
            profile[key] = value
    properties = re.findall(r"providers.gradleProperty\('(buzz\w+)'\).*getOrElse\(([^)]*)\)",
                            (ROOT/'android/app/build.gradle').read_text())
    if set(profile) != {name for name, _ in properties}:
        raise ValueError('Reference profile must explicitly classify every optional Gradle property')
    lines += ['', '## Android properties', '',
              'The reference column is the explicit phone comparison profile, not a claim',
              'that every enabled experiment independently increases FPS. A bare Gradle build',
              'uses the default column. Paths (`buzzWorkspace`, `buzzPython`, `sdlSource`) are required separately.', '',
              '| Gradle `-P` property | Bare default | Reference 29 |', '|---|---|---|']
    for name, default in properties:
        default = default.strip("'\"")
        lines.append(f'| `{name}` | `{default}` | `{profile[name]}` |')
    lines += ['', '## Embedded runtime values', '',
              'From `port/runtime_defaults.inc`. Common values install in standalone builds;',
              'the native-only section requires the qualified native profile. Explicit environment',
              'values win, including `0`. Android adds its conditional values described above.', '',
              '| Variable | Embedded value | Scope |', '|---|---|---|']
    scope = 'Common standalone / qualified native'
    for line in (ROOT/'port/runtime_defaults.inc').read_text().splitlines():
        if line.startswith('#ifndef BUZZ_COMMON_DEFAULTS_ONLY'):
            scope = 'Qualified native only'
        m = re.match(r'X\((\w+), "([^"]*)"\)', line)
        if m:
            lines.append(f'| `{m[1]}` | `{m[2]}` | {scope} |')
    return '\n'.join(lines)+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    path = ROOT/'docs/optimization-status.md'
    text = path.read_text(encoding='utf-8')
    prefix, tail = text.split(START, 1)
    _, suffix = tail.split(END, 1)
    expected = prefix+START+'\n\n'+tables()+'\n'+END+suffix
    if args.check:
        if expected != text:
            raise SystemExit('Optimization tables are stale; run tools/document-optimizations.py')
        print('Optimization catalog and Android profile coverage PASS')
    else:
        path.write_text(expected, encoding='utf-8')


if __name__ == '__main__':
    main()
