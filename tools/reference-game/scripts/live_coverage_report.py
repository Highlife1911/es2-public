"""Validate golden identity before merging original-branch coverage evidence."""
import argparse
import importlib.util
import json
from pathlib import Path

MODULE_HASH = '8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad'
CONDITIONS = {'je', 'jne', 'jl', 'jle', 'jg', 'jge', 'jb', 'ja', 'jns', 'jae', 'jbe', 'js', 'jp', 'jnp', 'jo', 'jno'}


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def integer(value, limit=2**64-1):
    return type(value) is int and 0 <= value <= limit


def outcome_seen(site, outcome):
    """An outcome counts when a suite observed it or the baseline knew it (edge mode)."""
    return bool(site[outcome] or site.get(f'known_{outcome}'))


def window_limit(golden, entry):
    """The frame_limit the proxy's report must carry: the golden's window, else the tick count of a held run.

    A suite with `hold` ends with the game thread parked after the N-th tick record (the entry's `hold_frame`);
    no exit callback runs, so its window is frozen at frame N-1 by the proxy's frame_limit N, which the golden
    (normalized without a window) does not name. Its last frame must be N-1. 0: the run ends by itself.
    """
    if golden.get('frame_limit'):
        return golden['frame_limit']
    hold = entry.get('hold_frame')
    if hold is None:
        return 0
    if type(hold) is not int or hold < 1 or golden.get('last_frame') != hold-1:
        raise ValueError(f"{entry['suite']}: hold_frame {hold!r} does not match the golden's last frame {golden.get('last_frame')!r}")
    return hold


def merge(manifest, oracle):
    result = {'schema': 1, 'valid': False, 'module_sha256': MODULE_HASH, 'suites': [], 'hooks': [], 'sites': []}
    # Edge mode with a baseline: outcomes the proxy was told not to watch.
    expected_known = {tuple(key.split('@')): tuple(value) for key, value in manifest.get('expected_known', {}).items()}
    inventory = None
    modes = None
    hooks = None
    proxy = None
    first_ticks = None
    merged = {}
    seen = set()
    for entry in manifest['suites']:
        name = entry['suite']
        if name in seen:
            raise ValueError(f'Duplicate suite: {name}')
        seen.add(name)
        comparison = oracle.compare(read(entry['golden']), read(entry['normalized']))
        if not comparison['identical'] or comparison['common_ticks'] == 0:
            raise ValueError(f'Coverage invalid: {name} differs from golden or has no compared ticks: {comparison["problems"]}')
        report = read(entry['coverage'])
        if report.get('schema') != 1 or report.get('module_sha256') != MODULE_HASH:
            raise ValueError(f'{name}: report schema/module identity')
        if report.get('mode') not in {'count', 'saturate', 'edge'} or report.get('error') != 0 or report.get('complete') is not True:
            raise ValueError(f'{name}: incomplete/failed coverage')
        edge = report['mode'] == 'edge'
        if expected_known and not edge:
            raise ValueError(f'{name}: a baseline (known outcomes) needs edge mode')
        if edge and report.get('known_outcomes') != sum(sum(v) for v in expected_known.values()):
            raise ValueError(f'{name}: the proxy did not load the expected baseline')
        if report.get('other_thread') != 0 or not report.get('tick_thread') or report.get('first_thread') not in (0, report['tick_thread']):
            raise ValueError(f'{name}: unverified simulation-thread ownership')
        if report.get('install_thread') != report['tick_thread'] or not report.get('has_tick'):
            raise ValueError(f'{name}: initialization/tick thread mismatch')
        if report.get('pid') != entry['pid']:
            raise ValueError(f'{name}: report process differs from identified launch')
        golden = read(entry['golden'])
        frame_limit = window_limit(golden, entry)
        if frame_limit:
            if report.get('reason') != 'frame_limit' or report.get('frame_limit') != frame_limit or report.get('last_frame') != frame_limit-1:
                raise ValueError(f'{name}: coverage not frozen at the validated frame boundary')
        elif report.get('reason') != 'exit' or report.get('frame_limit') != 0 or report.get('last_frame') != golden.get('last_frame'):
            raise ValueError(f'{name}: missing early exit finalization or last-frame mismatch')
        if not entry.get('proxy_sha256') or (proxy is not None and proxy != entry['proxy_sha256']):
            raise ValueError(f'{name}: proxy identity changed between suites')
        proxy = entry['proxy_sha256']
        tracking = report.get('first_hit_ticks', False)
        if type(tracking) is not bool or (first_ticks is not None and first_ticks != tracking) or tracking != manifest.get('expected_first_hit_ticks', False):
            raise ValueError(f'{name}: inconsistent first-hit tick configuration')
        first_ticks = tracking
        current_hooks = {h['name']: h['mode'] for h in report['hooks']}
        if len(current_hooks) != len(report['hooks']) or not current_hooks or any(type(m) is not int or m not in (0, 1, 2) for m in current_hooks.values()):
            raise ValueError(f'{name}: invalid hook selection')
        if hooks is not None and hooks != current_hooks:
            raise ValueError(f'{name}: hook selection/modes changed between suites')
        hooks = current_hooks
        if hooks != manifest['expected_hooks']:
            raise ValueError(f'{name}: report does not match the requested hook selection/modes')
        current = {}
        for site in report['sites']:
            key = (site['hook'], site['va'])
            if key in current or site['hook'] not in hooks or site['condition'] not in CONDITIONS:
                raise ValueError(f'{name}: duplicate/invalid site {key}')
            try:
                address = int(site['va'], 16)
            except (ValueError, TypeError):
                raise ValueError(f'{name}: invalid site address') from None
            if not 0x400000 <= address < 0x6d8000 or not all(integer(site[k]) for k in ('fall', 'taken')) or site['restored'] is not True:
                raise ValueError(f'{name}: invalid/unrestored site {key}')
            current[key] = site['condition']
            row = merged.setdefault(key, {'hook': key[0], 'va': key[1], 'condition': site['condition'], 'fall': 0, 'taken': 0,
                                          'first_fall_suite': None, 'first_taken_suite': None})
            if edge:
                known = (site.get('known_fall'), site.get('known_taken'))
                if known != expected_known.get(key, (False, False)):
                    raise ValueError(f'{name}: known outcomes of {key} differ from the baseline')
                row['known_fall'], row['known_taken'] = known
            for outcome in ('fall', 'taken'):
                if tracking:
                    row.setdefault(f'first_{outcome}_tick', None)
                first = site.get(f'first_{outcome}_tick')
                if tracking and ((not site[outcome] and first is not None) or (site[outcome] and (type(first) is not int or not -1 <= first <= report['last_frame']))):
                    raise ValueError(f'{name}: invalid first-hit tick {key}')
                row[outcome] += site[outcome]
                if site[outcome] and row[f'first_{outcome}_suite'] is None:
                    row[f'first_{outcome}_suite'] = name
                    if tracking:
                        row[f'first_{outcome}_tick'] = first
        expected = {(s['hook'], s['va']): s['condition'] for s in manifest['expected_sites']}
        if current != expected:
            raise ValueError(f'{name}: report does not include every requested branch site')
        if inventory is not None and inventory != current:
            raise ValueError(f'{name}: branch-site inventory changed between suites')
        if modes is not None and modes != report['mode']:
            raise ValueError(f'{name}: count/saturate modes cannot be mixed')
        inventory, modes = current, report['mode']
        result['suites'].append({'suite': name, 'golden_identical': True, 'compared_ticks': comparison['common_ticks'], 'pid': entry['pid']})
    if not seen:
        raise ValueError('No suites supplied')
    for name, mode in hooks.items():
        sites = [s for s in merged.values() if s['hook'] == name]
        result['hooks'].append(dict(hook=name, mode=mode, sites=len(sites), both_ways=sum(outcome_seen(s, 'fall') and outcome_seen(s, 'taken') for s in sites),
                                    fall_only=sum(outcome_seen(s, 'fall') and not outcome_seen(s, 'taken') for s in sites),
                                    taken_only=sum(outcome_seen(s, 'taken') and not outcome_seen(s, 'fall') for s in sites),
                                    never=sum(not outcome_seen(s, 'fall') and not outcome_seen(s, 'taken') for s in sites)))
    counts = {'count': 'exact within measured windows', 'saturate': 'lower bounds',
              'edge': 'lower bounds (one observation per outcome and role)'}[modes]
    result.update(valid=True, mode=modes, counts=counts, proxy_sha256=proxy, first_hit_ticks=first_ticks,
                  baseline_outcomes=sum(sum(v) for v in expected_known.values()), sites=list(merged.values()))
    return result


def table(summary):
    lines = ['| Hook | Sites | Both ways | Fall only | Taken only | Never |', '| --- | ---: | ---: | ---: | ---: | ---: |']
    for h in summary['hooks']:
        lines.append(f"| `{h['hook']}` | {h['sites']} | {h['both_ways']} | {h['fall_only']} | {h['taken_only']} | {h['never']} |")
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--oracle', type=Path, default=Path(__file__).with_name('oracle_regression.py'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('oracle_coverage_compare', args.oracle)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        result = merge(read(args.manifest), oracle)
    except (ValueError, KeyError, TypeError, OSError) as error:
        (args.output / 'coverage-summary.json').write_text(json.dumps({'valid': False, 'error': str(error)}, indent=2)+'\n')
        (args.output / 'coverage-summary.md').write_text('Coverage invalid: '+str(error)+'\n')
        print(str(error))
        return 1
    (args.output / 'coverage-summary.json').write_text(json.dumps(result, indent=2)+'\n')
    (args.output / 'coverage-summary.md').write_text(table(result))
    print(table(result), end='')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
