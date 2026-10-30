"""Print a pasteable hooks.md column from an accepted live coverage summary.

Accepts coverage-summary.json (run-live-coverage.ps1) and coverage-union.json
(run-oracle-parallel.ps1 -LiveCoverage); --totals appends a row over all hooks.
"""
import argparse
import json
from pathlib import Path
import re


def column(summary, totals=False):
    if summary.get('valid') is not True:
        raise ValueError('Only validated live coverage can be published')
    lines = ['| Hook | Live coverage: both ways / one way / never |', '| --- | ---: |']
    seen = set()
    sums = [0, 0, 0, 0]
    for hook in summary['hooks']:
        name = hook['hook']
        if not isinstance(name, str) or not re.fullmatch(r'[a-z][a-z0-9_]*', name) or name in seen:
            raise ValueError('Invalid or duplicate hook name')
        seen.add(name)
        fields = [hook[k] for k in ('sites', 'both_ways', 'fall_only', 'taken_only', 'never')]
        if any(type(n) is not int or n < 0 for n in fields) or fields[0] != sum(fields[1:]):
            raise ValueError(f'Inconsistent coverage totals: {name}')
        sums = [sums[0] + fields[0], sums[1] + fields[1], sums[2] + fields[2] + fields[3], sums[3] + fields[4]]
        lines.append(f"| `{name}` | {hook['both_ways']} / {hook['fall_only'] + hook['taken_only']} / {hook['never']} |")
    if not seen:
        raise ValueError('No hook coverage supplied')
    if totals:
        lines.append(f'| all {len(seen)} hooks ({sums[0]} sites) | {sums[1]} / {sums[2]} / {sums[3]} |')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('summary', type=Path)
    parser.add_argument('--totals', action='store_true', help='append a row with the sums over all hooks')
    args = parser.parse_args()
    try:
        print(column(json.loads(args.summary.read_text(encoding='utf-8-sig')), args.totals), end='')
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
