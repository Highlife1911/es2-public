"""Live branch-site union of one oracle regression.

run-oracle-parallel.ps1 -LiveCoverage run|original collects one proxy coverage
report per suite (<root>/<suite>/coverage.json beside the check-mode run in
<root>/<suite>/run). This helper builds the coverage manifest from that tree,
the hook name list and the branch-site inventory, validates every suite with
live_coverage_report.merge (golden identity, launch process,
finalized window, hook modes, complete site inventory) and writes

  <root>/<suite>/coverage-summary.json, .md   one suite
  <output>/coverage-union.json, .md           every suite (all must be valid)

Exit status 0 only for a valid union. The regression verdict is never changed
here. Meaning of the counts: docs/reference-game/live_coverage.md.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
BRANCH = re.compile(r'BRANCH\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\d+\s*,\s*(\w+)\s*,\s*0x[0-9a-fA-F]+\s*\)')
COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
MODES = {'original': 0, 'replaced': 1, 'compare': 2}
MEANING = {
    'original': ('Every hook original: the original branch outcomes the suites exercise. Golden identity of the '
                 'replaced-mode regression then says the replacements gave the same results for the same inputs; '
                 'it does not prove that a replacement follows the same internal paths.'),
    'run': ('Hook modes of the regression run: only executions that still reach original code count (hooks in '
            'original or compare mode, calls a replacement bypasses or skips to the original, sites in shared '
            'helpers that other original callers reach). A replaced hook\'s own path is never counted, so "never" '
            'is not evidence that a replacement path is unused.'),
}


def load(name):
    spec = importlib.util.spec_from_file_location(f'es2_union_{name}', HERE / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def hook_names(path):
    names = [line.strip() for line in Path(path).read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    if not names or len(set(names)) != len(names):
        raise ValueError(f'{path}: empty or duplicate hook names')
    return names


def branch_sites(path, names):
    """Every BRANCH(...) entry of branch_sites.h outside comments, as the proxy compiles it."""
    sites, seen = [], set()
    for m in BRANCH.finditer(COMMENT.sub(' ', Path(path).read_text(encoding='utf-8-sig'))):
        hook, va, condition = m.group(1), f'0x{int(m.group(2), 16):08x}', m.group(3)
        if hook not in names or va in seen:
            raise ValueError(f'{path}: unknown hook or duplicate site {hook} {va}')
        seen.add(va)
        sites.append(dict(hook=hook, va=va, condition=condition))
    return sites


def hook_modes(names, specification):
    """The [hooks] modes configure-tick-log.ps1 writes for a -Hooks string; omitted hooks stay original."""
    modes = {name: 0 for name in names}
    if not specification:
        return modes
    by_lower = {name.lower(): name for name in names}
    chosen = set()
    for item in specification.split(','):
        m = re.fullmatch(r'\s*(\w+)\s*=\s*(original|replaced|compare)\s*', item, re.I)
        name = by_lower.get(m.group(1).lower()) if m else None
        if name is None or name in chosen:
            raise ValueError(f'Malformed, unknown or duplicate hook selection: {item!r}')
        chosen.add(name)
        modes[name] = MODES[m.group(2).lower()]
    return modes


def suite_entry(root, suite, golden_root, specification):
    run = root / suite / 'run'
    entry = read(run / f'{suite}-entry.json')
    if entry.get('mode') != 'check' or (entry.get('hooks') or '') != specification:
        raise ValueError(f'{suite}: the run entry is not a check run with the expected hook selection')
    result = dict(suite=suite, golden=str(golden_root / suite / 'normalized.json'),
                  normalized=str(run / f'{suite}-normalized.json'), coverage=str(root / suite / 'coverage.json'),
                  pid=entry['launch']['pid'], proxy_sha256=entry['proxy_sha256'])
    if entry['launch'].get('hold_frame') is not None:   # a suite with `hold`: the window is its tick count
        result['hold_frame'] = entry['launch']['hold_frame']
    return result


def totals(summary):
    hooks = summary['hooks']
    count = {k: sum(h[k] for h in hooks) for k in ('sites', 'both_ways', 'fall_only', 'taken_only', 'never')}
    seen = load('live_coverage_report').outcome_seen
    count.update(hooks=len(hooks), hooks_with_sites=sum(1 for h in hooks if h['sites']),
                 outcomes_seen=sum(seen(s, 'fall') + seen(s, 'taken') for s in summary['sites']),
                 outcomes_total=2 * len(summary['sites']))
    return count


def union_markdown(union):
    t = union['totals']
    lines = [f"# Live branch-site union ({union['kind']})", '',
             f"Regression: `{union['regression']}`. Hook modes: {union['hook_modes']['original']} original, "
             f"{union['hook_modes']['replaced']} replaced, {union['hook_modes']['compare']} compare. "
             f"Coverage mode `{union['mode']}` ({union['counts']})."
             + (f" Baseline `{union['baseline']}`: {union['baseline_outcomes']} known outcomes were not watched "
                "and count as seen." if union.get('baseline') else ''), '',
             f"**{t['both_ways']} of {t['sites']} sites both ways, {t['fall_only']} fall only, "
             f"{t['taken_only']} taken only, {t['never']} never**; {t['outcomes_seen']} of {t['outcomes_total']} "
             f"outcomes seen; {t['hooks_with_sites']} of {t['hooks']} hooks have sites.", '',
             union['meaning'], '', '| Suite | Compared ticks |', '| --- | ---: |']
    lines += [f"| `{s['suite']}` | {s['compared_ticks']} |" for s in union['suites']]
    if union['kind'] == 'run' and union['residual_hooks']:
        lines += ['', 'Hooks not in original mode with original outcomes (residual original execution): '
                  + ', '.join(f'`{h}`' for h in union['residual_hooks']) + '.']
    names = {value: mode for mode, value in MODES.items()}
    lines += ['', '## Per hook', '', '| Hook | Mode | Sites | Both ways | Fall only | Taken only | Never |',
              '| --- | --- | ---: | ---: | ---: | ---: | ---: |']
    lines += [f"| `{h['hook']}` | {names[h['mode']]} | {h['sites']} | {h['both_ways']} | {h['fall_only']} | "
              f"{h['taken_only']} | {h['never']} |" for h in union['hooks']]
    lines += ['', '## Sites never reached', '']
    never = {}
    seen = load('live_coverage_report').outcome_seen
    for site in union['sites']:
        if not seen(site, 'fall') and not seen(site, 'taken'):
            never.setdefault(site['hook'], []).append(site['va'])
    if never:
        lines += ['| Hook | Never | Sites |', '| --- | ---: | --- |']
        lines += [f"| `{hook}` | {len(vas)} | {', '.join(vas)} |" for hook, vas in never.items()]
    else:
        lines.append('None.')
    return '\n'.join(lines) + '\n'


def known_outcomes(baseline, sites):
    """Outcomes a valid baseline union already saw, for the sites of the current inventory."""
    if baseline.get('valid') is not True:
        raise ValueError('The baseline union is not valid')
    seen = load('live_coverage_report').outcome_seen
    current = {(s['hook'], s['va']) for s in sites}
    known = {}
    for site in baseline['sites']:
        key = (site['hook'], site['va'])
        if key in current and (seen(site, 'fall') or seen(site, 'taken')):
            known[key] = (seen(site, 'fall'), seen(site, 'taken'))
    return known


def known_text(known):
    """The proxy's [coverage] known= file: one line per site with a known outcome."""
    return ''.join(f'{va} {int(fall)} {int(taken)}\n' for (_, va), (fall, taken) in sorted(known.items(), key=lambda i: i[0][1]))


def build(root, suites, golden_root, specification, names_path, sites_path, kind, coverage_mode=None, baseline=None):
    coverage, oracle = load('live_coverage_report'), load('oracle_regression')
    names = hook_names(names_path)
    modes = hook_modes(names, specification)
    if kind == 'original' and any(modes.values()):
        raise ValueError('An original-mode union needs every hook original')
    inventory = branch_sites(sites_path, names)
    base = dict(expected_hooks=modes, expected_sites=inventory, expected_first_hit_ticks=False)
    if baseline is not None:
        base['expected_known'] = {f'{h}@{va}': list(v) for (h, va), v in known_outcomes(read(baseline), inventory).items()}
    entries, status = [], []
    for suite in suites:
        directory = root / suite
        try:
            entry = suite_entry(root, suite, golden_root, specification)
            single = coverage.merge(dict(base, suites=[entry]), oracle)
            if coverage_mode and single['mode'] != coverage_mode:
                raise ValueError(f"{suite}: report mode {single['mode']} is not the requested {coverage_mode}")
        except (ValueError, KeyError, TypeError, OSError) as error:
            single = dict(schema=1, valid=False, suite=suite, error=str(error))
            if directory.is_dir():
                write_json(directory / 'coverage-summary.json', single)
                (directory / 'coverage-summary.md').write_text(f'Coverage invalid: {error}\n', encoding='utf-8')
            status.append(dict(suite=suite, valid=False, error=str(error)))
            continue
        write_json(directory / 'coverage-summary.json', single)
        (directory / 'coverage-summary.md').write_text(coverage.table(single), encoding='utf-8')
        entries.append(entry)
        status.append(dict(suite=suite, valid=True, summary=str(directory / 'coverage-summary.json')))
    if len(entries) != len(suites):
        bad = [s['suite'] for s in status if not s['valid']]
        return dict(schema=1, valid=False, kind=kind, regression=str(root), suite_status=status,
                    error='Coverage invalid for suite(s): ' + ', '.join(bad))
    union = coverage.merge(dict(base, suites=entries), oracle)
    union.update(kind=kind, regression=str(root), meaning=MEANING[kind], suite_status=status,
                 baseline=str(baseline) if baseline is not None else None,
                 hook_modes={mode: sum(1 for m in modes.values() if m == value) for mode, value in MODES.items()},
                 all_hooks_original=not any(modes.values()), totals=totals(union),
                 residual_hooks=[h['hook'] for h in union['hooks'] if h['mode'] and h['sites'] > h['never']])
    return union


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('root', type=Path, help='pass directory with <suite>/coverage.json and <suite>/run')
    parser.add_argument('--output', type=Path, required=True, help='directory for coverage-union.* (beside summary.json)')
    parser.add_argument('--suites', required=True, help='comma-separated suite names of the pass')
    parser.add_argument('--golden-root', type=Path, required=True)
    parser.add_argument('--hooks', default='', help='the -Hooks string of the pass (empty: every hook original)')
    parser.add_argument('--names', type=Path, required=True, help='proxy/hook-names.txt')
    parser.add_argument('--sites', type=Path, required=True, help='invitro/branch_sites.h')
    parser.add_argument('--kind', choices=sorted(MEANING), required=True)
    parser.add_argument('--coverage-mode', choices=('count', 'saturate', 'edge'), help='the [coverage] mode every report must have')
    parser.add_argument('--baseline', type=Path, help='coverage-union.json whose outcomes the pass did not watch (edge mode)')
    parser.add_argument('--write-known', type=Path, help='write the proxy known= file of --baseline for --sites and exit')
    args = parser.parse_args()
    if args.write_known:
        known = known_outcomes(read(args.baseline), branch_sites(args.sites, hook_names(args.names)))
        args.write_known.write_text(known_text(known), encoding='ascii', newline='\n')
        print(f'{sum(f + t for f, t in known.values())} known outcomes of {len(known)} sites: {args.write_known}')
        return 0
    suites = [s for s in args.suites.split(',') if s]
    try:
        if not suites or len(set(suites)) != len(suites):
            raise ValueError('Empty or duplicate suite list')
        union = build(args.root, suites, args.golden_root, args.hooks, args.names, args.sites, args.kind, args.coverage_mode,
                      args.baseline)
    except Exception as error:  # noqa: BLE001 - any failure must still leave an invalid union beside summary.json
        union = dict(schema=1, valid=False, kind=args.kind, regression=str(args.root), error=f'{type(error).__name__}: {error}')
    args.output.mkdir(parents=True, exist_ok=True)
    write_json(args.output / 'coverage-union.json', union)
    if not union['valid']:
        (args.output / 'coverage-union.md').write_text(f"Coverage union invalid: {union['error']}\n", encoding='utf-8')
        print(f"Coverage union invalid: {union['error']}")
        return 1
    (args.output / 'coverage-union.md').write_text(union_markdown(union), encoding='utf-8')
    t = union['totals']
    print(f"Coverage union ({args.kind}): {t['both_ways']} of {t['sites']} sites both ways, "
          f"{t['fall_only'] + t['taken_only']} one way, {t['never']} never: {args.output / 'coverage-union.json'}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
