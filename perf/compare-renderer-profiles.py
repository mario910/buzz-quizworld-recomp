"""Reject FPS comparisons with mismatched effective renderer settings.

Read stderr logs, not just requested settings. --vary explicitly permits a
field changed by the experiment (for example pool_spin_us). This guard does
not establish scene equivalence or replace EXE hashes and inspected captures.
"""
import argparse
import json
from pathlib import Path
import re


PATTERNS = {
    'gpu': r'\[RSX vulkan\] using device \d+: ([^\r\n]+)',
    'swapchain': r'\[RSX vulkan\] window swapchain (\d+x\d+),',
    'present_mode': r'\[RSX vulkan\] window present mode (\w+)',
    'batch_size': r'\[RSX vulkan\] experimental small-draw batch size (\d+)',
    'frame_cap': r'\[pc-settings\][^\r\n]* cap=(\d+)',
    'render_scale': r'\[pc-settings\][^\r\n]* render_scale=(\d+)%',
    'pool_spin_us': r'\[job-pool\] idle lanes poll (\d+) us',
}


def profile(text):
    result = {}
    for key, pattern in PATTERNS.items():
        values = sorted(set(re.findall(pattern, text)))
        if len(values) != 1:
            raise ValueError(f'{key}: expected one effective value, found {values}')
        result[key] = values[0]
    if '[RSX vulkan] two-context asynchronous draw batches enabled' not in text:
        raise ValueError('two draw contexts not confirmed')
    result['draw_contexts'] = '2'
    return result


def differences(profiles, vary=()):
    return {key: sorted({p[key] for p in profiles}) for key in profiles[0]
            if key not in vary and len({p[key] for p in profiles}) != 1}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    parser.add_argument('--vary', choices=list(PATTERNS), action='append', default=[])
    args = parser.parse_args()
    if len(args.logs) < 2:
        parser.error('provide at least two run logs')
    profiles, errors = {}, {}
    for path in args.logs:
        try:
            profiles[str(path)] = profile(path.read_text(errors='replace'))
        except (OSError, ValueError) as error:
            errors[str(path)] = str(error)
    mismatch = differences(list(profiles.values()), args.vary) if profiles else {}
    ok = not errors and not mismatch
    print(json.dumps({'comparable_settings': ok, 'allowed_variations': args.vary,
                      'profiles': profiles, 'mismatches': mismatch, 'errors': errors}, indent=2))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
