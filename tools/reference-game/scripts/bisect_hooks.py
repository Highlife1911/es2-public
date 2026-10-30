"""Bisect a failed hook regression down to the first offending hook.

A state machine the coordinator drives one regression step at a time:

  bisect_hooks.py plan   --regression <failed run dir> --state <state.json>
  bisect_hooks.py next   --state <state.json> --result <step run dir>
  bisect_hooks.py status --state <state.json>

`plan` reads the failed run of run-oracle-parallel.ps1 or
run-oracle-regression.ps1 (summary.json, per-suite entry, comparison and tick
log) and the hook registry, and prints the first step: a `-Hooks` selection
and the failing suites to run. After the coordinator has run that step into a
fresh directory, `next` reads it and prints the next step, until the first
offending hook is named. The method and its assumptions are documented in
docs/reference-game/oracle_regression.md ("Bisecting a failed regression").
Standard library only; never starts a process.
"""
import argparse
import json
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

SCHEMA = 1
MODES = ('original', 'replaced', 'compare')
TOOLS = Path(__file__).resolve().parents[1]
REGISTRY = TOOLS / 'proxy' / 'hook_registry.h'
NAMES = TOOLS / 'proxy' / 'hook-names.txt'
PARALLEL_RUNNER = TOOLS / 'setup' / 'run-oracle-parallel.ps1'
IMAGE_BASE = 0x400000
_REGISTRY_LINE = re.compile(r'^\s*ES2_HOOK\(\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,.*,\s*(\w+)\s*\)\s*$')


class BisectError(Exception):
    """Invalid input or state; nothing was changed."""


class Inconclusive(Exception):
    """A step result that neither passes nor fails; the state is unchanged."""


def read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding='utf-8-sig'))
    except FileNotFoundError:
        return None
    except (OSError, ValueError) as error:
        raise BisectError(f'Unreadable JSON {path}: {error}') from error


def write_json(path, data):
    # LF and UTF-8 without BOM; written next to the target, then replaced.
    path = Path(path)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_bytes((json.dumps(data, indent=1) + '\n').encode('utf-8'))
    os.replace(temporary, path)


# ---------------------------------------------------------------- registry

def load_registry(registry_path=REGISTRY, names_path=NAMES):
    """Return [(name, group, va)] in registry order, checked against the name list."""
    hooks = []
    for line in Path(registry_path).read_text(encoding='utf-8').splitlines():
        if not line.lstrip().startswith('ES2_HOOK('):
            continue
        match = _REGISTRY_LINE.match(line)
        if not match:
            raise BisectError(f'Unparsed registry line: {line.strip()}')
        hooks.append((match.group(1), match.group(3), IMAGE_BASE + int(match.group(2), 16)))
    names = [n.strip() for n in Path(names_path).read_text(encoding='utf-8').splitlines() if n.strip()]
    if [h[0] for h in hooks] != names or len(set(names)) != len(names) or not names:
        raise BisectError(f'{registry_path} and {names_path} disagree (regenerate hook-names.txt)')
    return hooks


def parse_selection(text, known):
    """Parse "name=mode,..." like configure-tick-log.ps1 does."""
    modes = {}
    if not text or not text.strip():
        return modes
    for entry in text.split(','):
        name, sep, mode = entry.partition('=')
        name, mode = name.strip().lower(), mode.strip().lower()
        if not sep or name not in known or mode not in MODES:
            raise BisectError(f'Malformed or unknown hook selection: {entry.strip()}')
        if name in modes:
            raise BisectError(f'Duplicate hook selection: {name}')
        modes[name] = mode
    return modes


def selection_text(order, baseline_modes, replaced):
    """Every hook of the failed selection with an explicit mode, in registry order.

    Hooks in `replaced` keep their failed-run mode (replaced or compare), all
    other hooks of the failed selection are `original`. Each hook carries its
    own mode: a nested hook is never implied by its caller's mode.
    """
    replaced = set(replaced)
    return ','.join(f'{n}={baseline_modes[n] if n in replaced else "original"}'
                    for n in order if n in baseline_modes)


# ---------------------------------------------------------- run directories

def _suite_files(run_dir, suite):
    """Locate one suite's files in a parallel (<suite>/run/) or serial layout."""
    for base in (run_dir / suite / 'run', run_dir):
        if (base / f'{suite}-entry.json').exists() or (base / f'{suite}-comparison.json').exists():
            return base
    return run_dir / suite / 'run' if (run_dir / suite).is_dir() else run_dir


def last_tick_counters(path, names):
    """Cumulative hk_<name>_{calls,bypassed,skipped} of the last tick record, or None."""
    path = Path(path)
    if not path.is_file():
        return None
    with path.open('rb') as stream:
        stream.seek(0, os.SEEK_END)
        position, tail = stream.tell(), b''
        while position > 0:
            size = min(1 << 20, position)
            position -= size
            stream.seek(position)
            tail = stream.read(size) + tail
            lines = tail.split(b'\n')
            # The first piece may be a partial line unless the file start was reached.
            for raw in reversed(lines if position == 0 else lines[1:]):
                if b'"event":"tick"' not in raw.replace(b' ', b''):
                    continue
                try:
                    record = json.loads(raw)
                except ValueError:
                    continue
                if record.get('event') != 'tick':
                    continue
                counters = {}
                for name in names:
                    values = [record.get(f'hk_{name}_{kind}') for kind in ('calls', 'bypassed', 'skipped')]
                    if values[0] is not None:
                        counters[name] = sum(v for v in values if isinstance(v, int))
                return counters
            tail = lines[0] if position else b''
    return None


def read_run(run_dir, names, suites=None):
    """Summarize a regression output directory.

    Per suite: `identical`, `different` (the comparison report exists and
    differs) or `error` (no comparison: launch, stall, normalization or runner
    failure; the summary result was DIFFERENT).
    """
    run_dir = Path(run_dir)
    summary = read_json(run_dir / 'summary.json')
    if not isinstance(summary, dict) or summary.get('mode') != 'check' or not isinstance(summary.get('results'), list):
        raise BisectError(f'{run_dir} has no check-mode summary.json')
    listed = {r.get('suite'): r.get('result') for r in summary['results'] if isinstance(r, dict)}
    if None in listed or len(listed) != len(summary['results']):
        raise BisectError(f'{run_dir}/summary.json has malformed results')
    wanted = list(listed) if suites is None else list(suites)
    result = dict(directory=str(run_dir), commit=summary.get('commit', ''), dirty_tree=summary.get('dirty_tree'),
                  hooks=summary.get('hooks') or '', suites={})
    for suite in wanted:
        if suite not in listed:
            raise BisectError(f'{run_dir}/summary.json has no result for suite {suite}')
        base = _suite_files(run_dir, suite)
        comparison = read_json(base / f'{suite}-comparison.json')
        entry = read_json(base / f'{suite}-entry.json')
        if listed[suite] == 'identical' and not (isinstance(comparison, dict) and comparison.get('identical') is False):
            outcome, detail = 'identical', ''
        elif isinstance(comparison, dict) and comparison.get('identical') is False:
            kinds = [p.get('kind', '?') for p in comparison.get('problems', []) if isinstance(p, dict)]
            outcome, detail = 'different', ','.join(dict.fromkeys(kinds)) or 'comparison differs'
        else:
            outcome, detail = 'error', f'summary result {listed[suite]!r} without a differing comparison report'
        if isinstance(entry, dict) and (entry.get('hooks') or '') != result['hooks']:
            outcome, detail = 'error', 'entry hooks differ from the summary hooks'
        result['suites'][suite] = dict(outcome=outcome, detail=detail,
                                       counters=last_tick_counters(base / f'{suite}.jsonl', names))
    return result


# ------------------------------------------------------------------- state

def _groups(hooks, members):
    """Split `members` into adapter groups, groups in order of first registry appearance."""
    order, groups = [], {}
    for name, group, _ in hooks:
        if name in members:
            if group not in groups:
                order.append(group)
                groups[group] = []
            groups[group].append(name)
    return [dict(label=g, hooks=groups[g]) for g in order]


def _search(pool, level, items, base, known_pass):
    # Prefix search: prefix `lo` is known (or, for -1, not yet known) to pass,
    # prefix `hi` (= all items) reproduces the failure.
    return dict(pool=pool, level=level, items=items, base=base, lo=0 if known_pass else -1, hi=len(items))


def _replaced_for(state, search, prefix):
    kept = state['kept'] if search['pool'] == 'active' else []
    chosen = set(kept) | set(search['base'])
    for item in search['items'][:prefix]:
        chosen.update(item['hooks'])
    return chosen


def _make_step(state, kind, replaced, description, prefix=None):
    number = len(state['steps']) + 1
    order = [h['name'] for h in state['registry']]
    directory = Path(state['step_root']) / f'{state["name"]}-step{number:02d}'
    return dict(number=number, kind=kind, prefix=prefix, description=description,
                replaced=[n for n in order if n in replaced],
                hooks=selection_text(order, state['baseline']['modes'], replaced),
                suites=list(state['baseline']['failing']), output=str(directory))


def _describe_prefix(state, search, prefix):
    items = search['items']
    noun = 'group' if search['level'] == 'group' else 'hook'
    shown = ', '.join(i['label'] for i in items[:prefix]) or 'none'
    scope = 'kept (never entered) hooks' if search['pool'] == 'kept' else 'entered hooks'
    where = f' in group {state["offending_group"]}' if search['level'] == 'hook' else ''
    return f'{noun} prefix {prefix}/{len(items)} of the {scope}{where}: {shown}'


def advance(state):
    """Move the search forward until a step is needed or the result is known."""
    while state['pending'] is None and state['result'] is None:
        search = state['search']
        if search['hi'] - search['lo'] > 1:
            prefix = (search['lo'] + search['hi']) // 2
            replaced = _replaced_for(state, search, prefix)
            state['pending'] = _make_step(state, 'prefix', replaced, _describe_prefix(state, search, prefix), prefix)
            continue
        if search['hi'] == 0:
            # Only the kept hooks replaced already fails: one of them is
            # entered once its caller runs original code. Search them.
            if search['pool'] != 'active' or not state['kept']:
                raise BisectError('Inconsistent state: prefix 0 failed without kept hooks')
            state['notes'].append('The kept hooks alone reproduce the failure; searching them.')
            state['search'] = _search('kept', 'group', _groups_from_state(state, state['kept']), [], True)
            continue
        index = search['hi'] - 1
        item = search['items'][index]
        earlier = [h for i in search['items'][:index] for h in i['hooks']]
        if search['level'] == 'group':
            state['offending_group'] = item['label']
            state['search'] = _search(search['pool'], 'hook', [dict(label=h, hooks=[h]) for h in item['hooks']],
                                      search['base'] + earlier, True)
            continue
        hook = item['hooks'][0]
        prefix_hooks = search['base'] + earlier
        state['found'] = dict(hook=hook, group=state['offending_group'], needs_prefix=prefix_hooks)
        if prefix_hooks and state['confirm']:
            kept = state['kept'] if search['pool'] == 'active' else []
            state['pending'] = _make_step(state, 'confirm', set(kept) | {hook},
                                          f'confirm: only {hook}' + (' and the kept hooks' if kept else '') + ' replaced')
        else:
            finish(state, standalone=True if not prefix_hooks else None)


def _groups_from_state(state, members):
    return _groups([(h['name'], h['group'], h['va']) for h in state['registry']], set(members))


def finish(state, standalone):
    found = state['found']
    info = next(h for h in state['registry'] if h['name'] == found['hook'])
    baseline_calls = {s: (c or {}).get(found['hook']) for s, c in state['baseline']['counters'].items()}
    state['result'] = dict(hook=found['hook'], group=found['group'], va=f'0x{info["va"]:06x}',
                           standalone=standalone, needs_prefix=[] if standalone else found['needs_prefix'],
                           baseline_calls=baseline_calls)


def plan(regression, state_path, registry=REGISTRY, names=NAMES, keep_dormant=True, confirm=True,
         step_root=None, name=None, errors_fail=None):
    state_path = Path(state_path)
    if state_path.exists():
        raise BisectError(f'State file exists: {state_path}')
    hooks = load_registry(registry, names)
    known = [h[0] for h in hooks]
    run = read_run(regression, known)
    modes = parse_selection(run['hooks'], set(known))
    active_modes = {n: m for n, m in modes.items() if m != 'original'}
    if not active_modes:
        raise BisectError('The failed run selected no non-original hook; the difference does not come from hooks')
    failing = [s for s, r in run['suites'].items() if r['outcome'] != 'identical']
    if not failing:
        raise BisectError(f'{regression} has no failing suite')
    counters = {s: run['suites'][s]['counters'] for s in failing}
    notes = []
    dormant = []
    if keep_dormant:
        if all(c is not None for c in counters.values()):
            dormant = [n for n in known if n in active_modes and all(c.get(n) == 0 for c in counters.values())]
        else:
            notes.append('A failing suite has no tick log with hook counters; no hook is kept, all are searched.')
    if errors_fail is None:
        errors_fail = any(run['suites'][s]['outcome'] == 'error' for s in failing)
        if errors_fail:
            notes.append('The failed run has suites without a comparison report (crash, stall or runner error); '
                         'an error in a step counts as a failure.')
    regression = Path(regression).resolve()
    state = dict(
        schema=SCHEMA, created_utc=datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
        name=name or f'bisect-{regression.name}', step_root=str(Path(step_root).resolve() if step_root else regression.parent),
        confirm=confirm, errors_fail=errors_fail,
        registry=[dict(name=n, group=g, va=va) for n, g, va in hooks],
        baseline=dict(directory=str(regression), commit=run['commit'], dirty_tree=run['dirty_tree'], hooks=run['hooks'],
                      modes={n: modes[n] for n in known if n in modes},
                      failing=failing, outcomes={s: r['outcome'] for s, r in run['suites'].items()},
                      counters=counters),
        kept=dormant, notes=notes, steps=[], pending=None, found=None, offending_group=None, result=None)
    candidates = [n for n in known if n in active_modes and n not in dormant]
    if candidates:
        state['search'] = _search('active', 'group', _groups(hooks, set(candidates)), [], not dormant)
    else:
        state['notes'].append('No selected hook was entered in the failing suites; searching all of them.')
        state['kept'] = []
        state['search'] = _search('active', 'group', _groups(hooks, set(dormant)), [], True)
    advance(state)
    write_json(state_path, state)
    return state


def load_state(state_path):
    state = read_json(state_path)
    if not isinstance(state, dict) or state.get('schema') != SCHEMA:
        raise BisectError(f'No bisect state of schema {SCHEMA}: {state_path}')
    return state


def evaluate(step, run, errors_fail):
    outcomes = {s: run['suites'][s]['outcome'] for s in step['suites']}
    if 'different' in outcomes.values():
        return 'fail'
    if 'error' in outcomes.values():
        if errors_fail:
            return 'fail'
        bad = ', '.join(f'{s} ({run["suites"][s]["detail"]})' for s, o in outcomes.items() if o == 'error')
        raise Inconclusive(f'Step {step["number"]} has errors without a difference: {bad}. Run the step again into a '
                           'fresh directory, or pass --errors-fail if the error is the failure being bisected.')
    return 'pass'


def next_step(state_path, result_dir, errors_fail=None):
    state = load_state(state_path)
    step = state['pending']
    if step is None:
        raise BisectError('The bisect is finished; see status')
    result_dir = Path(result_dir).resolve()
    if any(Path(s['result']['directory']) == result_dir for s in state['steps']) or result_dir == Path(state['baseline']['directory']):
        raise BisectError(f'{result_dir} was already used')
    names = [h['name'] for h in state['registry']]
    run = read_run(result_dir, names, step['suites'])
    if run['hooks'] != step['hooks']:
        raise BisectError(f'{result_dir} ran a different hook selection than step {step["number"]}')
    base = state['baseline']
    if run['commit'] != base['commit'] or run['dirty_tree'] != base['dirty_tree']:
        raise BisectError(f'{result_dir} ran commit {run["commit"]} (dirty={run["dirty_tree"]}), the failed run '
                          f'{base["commit"]} (dirty={base["dirty_tree"]}); a bisect needs the same build')
    verdict = evaluate(step, run, state['errors_fail'] if errors_fail is None else errors_fail)
    warnings = []
    if state['search']['pool'] == 'active':
        for suite, data in run['suites'].items():
            for hook in state['kept']:
                calls = (data['counters'] or {}).get(hook)
                if calls:
                    warnings.append(f'kept hook {hook} was entered {calls} times in {suite}')
    step = dict(step, verdict=verdict, warnings=warnings,
                result=dict(directory=str(result_dir), suites={s: dict(outcome=r['outcome'], detail=r['detail'])
                                                                for s, r in run['suites'].items()}))
    state['steps'].append(step)
    state['pending'] = None
    if step['kind'] == 'prefix':
        search = state['search']
        if verdict == 'fail':
            search['hi'] = step['prefix']
        else:
            search['lo'] = step['prefix']
        advance(state)
    else:
        finish(state, standalone=verdict == 'fail')
    write_json(state_path, state)
    return state


# ------------------------------------------------------------------ output

def _ps_quote(text):
    return "'" + str(text).replace("'", "''") + "'"


def step_command(state, step, state_path):
    suites = ','.join(_ps_quote(s) for s in step['suites'])
    run = (f'& {_ps_quote(PARALLEL_RUNNER)} -Suite {suites} -Hooks {_ps_quote(step["hooks"])} '
           f'-OutputDirectory {_ps_quote(step["output"])}')
    follow = (f'& {_ps_quote(sys.executable)} -B {_ps_quote(Path(__file__).resolve())} next '
              f'--state {_ps_quote(Path(state_path).resolve())} --result {_ps_quote(step["output"])}')
    return run, follow


def render(state, state_path):
    lines = [f'Bisect {state["name"]}: failed run {state["baseline"]["directory"]} '
             f'(commit {state["baseline"]["commit"][:12]}), failing suites: {", ".join(state["baseline"]["failing"])}']
    selected = [n for n, m in state['baseline']['modes'].items() if m != 'original']
    lines.append(f'  {len(selected)} non-original hooks; {len(state["kept"])} never entered in the failing suites '
                 'and kept at their mode' + (f': {", ".join(state["kept"])}' if state['kept'] else ''))
    lines += [f'  note: {n}' for n in state['notes']]
    for step in state['steps']:
        outcomes = ', '.join(f'{s} {r["outcome"]}' + (f' ({r["detail"]})' if r['detail'] else '')
                             for s, r in step['result']['suites'].items())
        lines.append(f'  step {step["number"]:2d} {step["verdict"].upper():4s} {step["description"]} [{outcomes}]')
        lines += [f'          warning: {w}' for w in step['warnings']]
    if state['result']:
        result = state['result']
        lines.append(f'First offending hook: {result["hook"]} (group {result["group"]}, {result["va"]}); '
                     f'calls in the failed run: {result["baseline_calls"]}')
        if result['standalone'] is True:
            lines.append('  It fails on its own (with the kept hooks), all other hooks original.')
        elif result['standalone'] is False:
            lines.append('  It passes on its own: the failure needs it together with some of: '
                         + ', '.join(result['needs_prefix']))
        else:
            lines.append('  Not confirmed in isolation (--no-confirm); replaced before it in the search: '
                         + ', '.join(result['needs_prefix']))
        if any(s['warnings'] for s in state['steps']):
            lines.append('  Kept hooks were entered in some steps; check the warnings before trusting the result.')
        lines.append('Further offenders: set this hook original (or fix it) and run the regression again.')
    elif state['pending']:
        step = state['pending']
        run, follow = step_command(state, step, state_path)
        lines += [f'Next: step {step["number"]}, {step["description"]}',
                  f'  {len(step["replaced"])} hooks non-original, suites: {", ".join(step["suites"])}',
                  f'  run:  {run}', f'  then: {follow}']
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('plan', help='start a bisect from a failed regression directory')
    p.add_argument('--regression', required=True, help='failed run-oracle-parallel/-regression output directory')
    p.add_argument('--state', required=True, help='new state file')
    p.add_argument('--registry', default=str(REGISTRY))
    p.add_argument('--names', default=str(NAMES))
    p.add_argument('--step-root', help='parent of the step output directories (default: next to the failed run)')
    p.add_argument('--name', help='step directory prefix (default: bisect-<failed run name>)')
    p.add_argument('--no-keep', action='store_true', help='search hooks never entered in the failing suites too')
    p.add_argument('--no-confirm', action='store_true', help='skip the isolation step for the found hook')
    p.add_argument('--errors-fail', action='store_true', default=None,
                   help='count suites without a comparison report as failures')
    n = sub.add_parser('next', help='read one step result and print the next step')
    n.add_argument('--state', required=True)
    n.add_argument('--result', required=True, help='output directory of the pending step')
    n.add_argument('--errors-fail', action='store_true', default=None)
    s = sub.add_parser('status', help='print the history and the pending step')
    s.add_argument('--state', required=True)
    for command in (p, n, s):
        command.add_argument('--json', action='store_true', help='print the state as JSON')
    args = parser.parse_args(argv)
    try:
        if args.command == 'plan':
            state = plan(args.regression, args.state, args.registry, args.names, not args.no_keep, not args.no_confirm,
                         args.step_root, args.name, args.errors_fail)
        elif args.command == 'next':
            state = next_step(args.state, args.result, args.errors_fail)
        else:
            state = load_state(args.state)
    except Inconclusive as error:
        print(f'bisect_hooks: {error}', file=sys.stderr)
        return 3
    except BisectError as error:
        print(f'bisect_hooks: {error}', file=sys.stderr)
        return 2
    print(json.dumps(state, indent=1) if args.json else render(state, args.state))
    return 0


if __name__ == '__main__':
    sys.exit(main())
