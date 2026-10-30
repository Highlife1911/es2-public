"""Validate entry-probe reports against unchanged oracle goldens, then merge."""
import argparse
import importlib.util
import json
from pathlib import Path

MODULE_HASH = '8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad'
# 'ret' keys the histogram by the return address instead of a vtable.
REGISTERS = {'', 'esp4', 'eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi', 'ret'}
RECEIVER_SLOTS = 128  # EP_RECEIVERS of proxy/entry_probe_engine.h


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def integer(n, maximum=2**63-1):
    return type(n) is int and 0 <= n <= maximum


def address(text):
    return isinstance(text, str) and len(text) == 10 and text.startswith('0x') and all(c in '0123456789abcdef' for c in text[2:])


def histogram(s):
    """Key -> count of one report entry, checked (every hit keyed or overflowed)."""
    count, overflow = s['count'], s['overflow']
    if not integer(count) or not integer(overflow) or overflow > count or type(s['saturated']) is not bool:
        raise ValueError('Invalid counters')
    if len(s['receivers']) > RECEIVER_SLOTS:
        raise ValueError('Histogram exceeds capacity')
    keys, total = {}, overflow
    for h in s['receivers']:
        if not address(h['vtable']) or h['vtable'] == '0x00000000' or h['vtable'] in keys or not integer(h['count']) or not h['count']:
            raise ValueError('Invalid receiver histogram')
        keys[h['vtable']] = h['count']; total += h['count']
    if total != (count if s['receiver'] else 0):
        raise ValueError('Histogram/overflow does not account for every hit')
    return keys


def load_split(name, entry, r, final):
    """The first-tick report (arming to the first frame) of one suite, checked against its final report."""
    load = read(entry['probes_load'])
    for key in ('schema', 'module_sha256', 'mode', 'saturate_after', 'pid', 'install_thread', 'frame_limit'):
        if load.get(key) != r.get(key):
            raise ValueError(f'{name}: first-tick report identity mismatch')
    if load.get('reason') != 'first_tick' or load.get('complete') is not False or load.get('error') != 0 or load.get('has_tick') is not True             or load.get('other_thread') != 0 or load.get('tick_thread') != r.get('tick_thread') or not integer(load.get('last_frame'), 2**32-1)             or load['last_frame'] > r['last_frame']:
        raise ValueError(f'{name}: invalid first-tick report')
    entries = {s['va']: s for s in load['entries']}
    if len(entries) != len(load['entries']) or set(entries) != set(final):
        raise ValueError(f'{name}: first-tick entries differ')
    out = {}
    for va, s in entries.items():
        if s.get('receiver') != final[va]['receiver']:
            raise ValueError(f'{name}: first-tick receiver differs')
        keys, later = histogram(s), final[va]
        if s['count'] > later['count'] or s['overflow'] > later['overflow'] or any(n > later['keys'].get(k, 0) for k, n in keys.items()):
            raise ValueError(f'{name}: first-tick count above the final count')
        out[va] = {k: s[k] for k in ('count', 'saturated', 'receivers', 'overflow')}
    return load['last_frame'], out


def merge(manifest, oracle):
    expected = manifest['expected_entries']
    if not expected or len({s['va'] for s in expected}) != len(expected):
        raise ValueError('Empty/duplicate requested entries')
    mode, limit = manifest['probe_mode'], manifest['saturate_after']
    if mode not in {'count', 'saturate'} or not integer(limit, 2**32-1) or not limit:
        raise ValueError('Invalid requested mode/limit')
    result = dict(schema=1, valid=False, module_sha256=MODULE_HASH, mode=mode, saturate_after=limit, suites=[], entries=[])
    rows = {e['va']: dict(e, suites={}) for e in expected}
    seen, proxy = set(), None
    for entry in manifest['suites']:
        name = entry['suite']
        if name in seen:
            raise ValueError('Duplicate suite')
        seen.add(name)
        golden = read(entry['golden'])
        comparison = oracle.compare(golden, read(entry['normalized']))
        if not comparison['identical'] or not comparison['common_ticks']:
            raise ValueError(f'{name}: perturbed/empty suite, probe data invalid')
        r = read(entry['probes'])
        if r.get('schema') != 1 or r.get('module_sha256') != MODULE_HASH or r.get('mode') != mode or r.get('saturate_after') != limit:
            raise ValueError(f'{name}: identity/configuration mismatch')
        if r.get('complete') is not True or r.get('error') != 0 or r.get('pid') != entry['pid'] or r.get('other_thread') != 0:
            raise ValueError(f'{name}: incomplete/error/process/thread mismatch')
        thread = r.get('tick_thread')
        if not integer(thread, 2**32-1) or not thread or r.get('install_thread') != thread or r.get('first_thread') not in (0, thread) or r.get('has_tick') is not True:
            raise ValueError(f'{name}: unverified ownership/window')
        frame_limit = golden.get('frame_limit') or 0
        if not frame_limit and entry.get('hold_frame') is not None:
            # A held run (suite field hold) is frozen at its tick count; see live_coverage_report.window_limit.
            frame_limit = entry['hold_frame']
            if type(frame_limit) is not int or frame_limit < 1 or golden.get('last_frame') != frame_limit-1:
                raise ValueError(f'{name}: hold_frame does not match the golden last frame')
        if r.get('frame_limit') != frame_limit or r.get('last_frame') != (frame_limit-1 if frame_limit else golden['last_frame']) or r.get('reason') != ('frame_limit' if frame_limit else 'exit'):
            raise ValueError(f'{name}: finalization window mismatch')
        if not entry.get('proxy_sha256') or (proxy is not None and proxy != entry['proxy_sha256']):
            raise ValueError('Proxy changed between suites')
        proxy = entry['proxy_sha256']
        found = {}
        for s in r['entries']:
            va = s['va']
            if va not in rows or va in found or not address(va) or s.get('receiver') != rows[va]['receiver'] or s['receiver'] not in REGISTERS:
                raise ValueError(f'{name}: invalid entry/receiver')
            keys = histogram(s)
            count = s['count']
            if s['saturated'] != (mode == 'saturate' and count == limit) or (mode == 'saturate' and count > limit):
                raise ValueError('Invalid saturation')
            found[va] = dict(receiver=s['receiver'], count=count, overflow=s['overflow'], keys=keys)
            rows[va]['suites'][name] = {k: s[k] for k in ('count', 'saturated', 'receivers', 'overflow')}
        if set(found) != set(rows):
            raise ValueError('Missing requested entry')
        suite = dict(suite=name, golden_identical=True, compared_ticks=comparison['common_ticks'], last_frame=r['last_frame'])
        if entry.get('probes_load'):  # the counts split at the first tick
            suite['first_frame'], load = load_split(name, entry, r, found)
            for va, s in load.items():
                rows[va]['suites'][name]['load'] = s
        result['suites'].append(suite)
    if not seen:
        raise ValueError('No suites')
    result.update(valid=True, proxy_sha256=proxy, entries=list(rows.values()))
    return result


def table(result):
    lines = ['| Entry | Receiver | Suite | Count | Saturated | Vtable counts | Overflow |', '| --- | --- | --- | ---: | --- | --- | ---: |']
    for e in result['entries']:
        for suite, s in e['suites'].items():
            receivers = ', '.join(f"{r['vtable']}: {r['count']}" for r in s['receivers']) or 'none'
            lines.append(f"| `{e['va']}` | {e['receiver'] or 'off'} | {suite} | {s['count']} | {s['saturated']} | {receivers} | {s['overflow']} |")
    return '\n'.join(lines)+'\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('manifest', type=Path); p.add_argument('--output', required=True, type=Path)
    p.add_argument('--oracle', type=Path, default=Path(__file__).with_name('oracle_regression.py'))
    a = p.parse_args()
    spec = importlib.util.spec_from_file_location('oracle_probe_compare', a.oracle)
    oracle = importlib.util.module_from_spec(spec); spec.loader.exec_module(oracle)
    a.output.mkdir(parents=True, exist_ok=True)
    try:
        result = merge(read(a.manifest), oracle)
        text = table(result)
    except (ValueError, KeyError, TypeError, OSError) as error:
        result = dict(valid=False, error=str(error)); text = 'Probe data invalid: '+str(error)+'\n'
    (a.output/'probes-summary.json').write_text(json.dumps(result, indent=2)+'\n')
    (a.output/'probes-summary.md').write_text(text)
    print(text, end='')
    return 0 if result['valid'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
