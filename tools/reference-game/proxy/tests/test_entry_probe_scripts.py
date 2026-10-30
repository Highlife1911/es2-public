# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Own synthetic reports; the instruction inventory is tested in scripts/tests/test_probe_instruction_inventory.py."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def main():
    helper, runner, oracle_path = map(Path, sys.argv[1:])
    helper = helper.with_name('live_probe_report.py')
    probes = load('probe_report', helper); oracle = load('oracle', oracle_path)
    powershell = Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
    with tempfile.TemporaryDirectory(dir=os.environ['TEST_TMPDIR']) as tmp:
        root = Path(tmp); source = root/'inputs'; source.mkdir()
        def write(path, data): path.write_text(json.dumps(data), encoding='utf-8')
        expected = [dict(va='0x0040109c', receiver='esp4'), dict(va='0x0040111c', receiver='')]
        golden = dict(format=1, frame_limit=2, tick_fields=['frame', 'clock'], ticks=[[0, 0], [1, 47]], watches=[], outputs={}, last_frame=1)
        entries = []
        for i, suite in enumerate(('acceptance-60s', 'c1_01-collision')):
            g, n, p = (source/(suite+suffix+'.json') for suffix in ('-golden', '-normalized', '-probes'))
            write(g, golden); write(n, golden)
            sites = [dict(expected[0], count=2**32+1+i, saturated=False, receivers=[dict(vtable='0x00600000', count=2**32+i)], overflow=1),
                     dict(expected[1], count=2+i, saturated=False, receivers=[], overflow=0)]
            report = dict(schema=1, module_sha256=probes.MODULE_HASH, mode='count', saturate_after=3, pid=100+i, install_thread=9,
                          first_thread=9, other_thread=0, tick_thread=9, error=0, complete=True, reason='frame_limit', has_tick=True,
                          last_frame=1, frame_limit=2, entries=sites)
            write(p, report)
            entries.append(dict(suite=suite, golden=str(g), normalized=str(n), probes=str(p), pid=100+i, proxy_sha256='synthetic'))
        manifest = dict(suites=entries, expected_entries=expected, probe_mode='count', saturate_after=3)
        write(source/'probes-manifest.json', manifest)
        assert probes.merge(manifest, oracle)['valid']
        command = [str(powershell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(runner), '-ProbesOnly',
                   '-ProbeEntries', '0040109c:rcv=esp4,0040111c', '-SaturateAfter', '3', '-MockDirectory', str(source),
                   '-OutputDirectory', str(root/'output'), '-Python', sys.executable]
        run = subprocess.run(command, capture_output=True, text=True, errors='replace')
        assert run.returncode == 0, (run.stdout, run.stderr)
        summary = probes.read(root/'output/probes-summary.json')
        assert summary['valid'] and summary['entries'][0]['suites']['acceptance-60s']['count'] == 2**32+1
        # Any changed simulation value invalidates the entire measurement.
        bad = copy.deepcopy(golden); bad['ticks'][1][1] = 48; write(Path(entries[0]['normalized']), bad)
        failed = command.copy(); failed[failed.index('-OutputDirectory')+1] = str(root/'bad')
        assert subprocess.run(failed, capture_output=True).returncode != 0
        assert probes.read(root/'bad/probes-summary.json')['valid'] is False
        write(Path(entries[0]['normalized']), golden)
        report_path = Path(entries[0]['probes']); saved = probes.read(report_path)
        mutations = [('schema', 2), ('module_sha256', 'wrong'), ('mode', 'saturate'), ('saturate_after', 2), ('pid', 99),
                     ('error', 1), ('complete', False), ('first_thread', 4), ('other_thread', 3), ('install_thread', 1),
                     ('tick_thread', 0), ('has_tick', False), ('reason', 'detach'), ('last_frame', 3), ('frame_limit', 3)]
        rejected = 0
        for field, value in mutations:
            bad = copy.deepcopy(saved); bad[field] = value; write(report_path, bad)
            try: probes.merge(manifest, oracle)
            except ValueError: rejected += 1
            else: raise AssertionError(field)
        for case in ('duplicate', 'missing', 'receiver', 'count', 'overflow', 'saturated', 'histogram', 'vtable'):
            bad = copy.deepcopy(saved)
            if case == 'duplicate': bad['entries'].append(bad['entries'][0])
            elif case == 'missing': bad['entries'].pop()
            elif case == 'receiver': bad['entries'][0]['receiver'] = 'eax'
            elif case == 'count': bad['entries'][0]['count'] = -1
            elif case == 'overflow': bad['entries'][0]['overflow'] += 1
            elif case == 'saturated': bad['entries'][0]['saturated'] = True
            elif case == 'histogram': bad['entries'][0]['receivers'] *= 2
            else: bad['entries'][0]['receivers'][0]['vtable'] = 'invalid'
            write(report_path, bad)
            try: probes.merge(manifest, oracle)
            except ValueError: rejected += 1
            else: raise AssertionError(case)
        write(report_path, saved)
        # A held suite has no golden frame_limit and no exit: its window is the launch's hold_frame.
        held_golden = copy.deepcopy(golden); held_golden['frame_limit'] = None
        write(Path(entries[0]['golden']), held_golden); write(Path(entries[0]['normalized']), held_golden)
        held = copy.deepcopy(manifest); held['suites'][0]['hold_frame'] = 2
        assert probes.merge(held, oracle)['valid']
        for hold in (3, 0, '2'):
            held['suites'][0]['hold_frame'] = hold
            try: probes.merge(held, oracle)
            except ValueError: rejected += 1
            else: raise AssertionError(('hold', hold))
        try: probes.merge(manifest, oracle)
        except ValueError: rejected += 1
        else: raise AssertionError('held report accepted without hold_frame')
        write(Path(entries[0]['golden']), golden); write(Path(entries[0]['normalized']), golden)
        # Return-address keys and first-tick reports (the counts before the first frame).
        split = copy.deepcopy(manifest); split['expected_entries'][0]['receiver'] = 'ret'
        for entry in split['suites']:
            final = probes.read(entry['probes']); final['entries'][0]['receiver'] = 'ret'
            final['entries'][0]['receivers'] = [dict(vtable='0x00434ed2', count=2**32-1), dict(vtable='0x0043dce9', count=final['entries'][0]['count']-2**32)]
            entry['probes'] = str(Path(entry['probes']).with_suffix('.ret.json')); write(Path(entry['probes']), final)
            first = copy.deepcopy(final); first.update(complete=False, reason='first_tick', last_frame=0)
            first['entries'][0].update(count=5, overflow=0, receivers=[dict(vtable='0x00434ed2', count=5)]); first['entries'][1]['count'] = 2
            entry['probes_load'] = str(Path(entry['probes']).with_suffix('.load.json')); write(Path(entry['probes_load']), first)
        merged = probes.merge(split, oracle)
        assert merged['valid'] and merged['suites'][0]['first_frame'] == 0 and merged['suites'][0]['last_frame'] == 1
        assert merged['entries'][0]['suites']['acceptance-60s']['load']['count'] == 5
        assert merged['entries'][1]['suites']['c1_01-collision']['load']['count'] == 2
        load_path = Path(split['suites'][0]['probes_load']); first = probes.read(load_path)
        for case in ('reason', 'complete', 'above', 'key', 'frame', 'missing'):
            bad = copy.deepcopy(first)
            if case == 'reason': bad['reason'] = 'exit'
            elif case == 'complete': bad['complete'] = True
            elif case == 'above': bad['entries'][1]['count'] = 3
            elif case == 'key': bad['entries'][0]['receivers'][0]['vtable'] = '0x00401000'
            elif case == 'frame': bad['last_frame'] = 2
            else: bad['entries'].pop()
            write(load_path, bad)
            try: probes.merge(split, oracle)
            except ValueError: rejected += 1
            else: raise AssertionError('first tick: '+case)
        # A second synthetic manifest makes the actual wrapper merge both.
        coverage = load('coverage', helper.with_name('live_coverage_report.py'))
        coverage_entries = []
        for entry in entries:
            p = source/(entry['suite']+'-coverage.json')
            r = probes.read(entry['probes']); r.update(hooks=[dict(name='rng_next', mode=0)], sites=[])
            write(p, r); coverage_entries.append(dict(entry, coverage=str(p)))
        write(source/'coverage-manifest.json', dict(suites=coverage_entries, expected_hooks={'rng_next': 0}, expected_sites=[]))
        both = command.copy(); both.remove('-ProbesOnly'); both[both.index('-OutputDirectory')+1] = str(root/'both')
        run = subprocess.run(both, capture_output=True, text=True, errors='replace')
        assert run.returncode == 0, (run.stdout, run.stderr)
        assert probes.read(root/'both/probes-summary.json')['valid'] and coverage.read(root/'both/coverage-summary.json')['valid']
        dry = command[:command.index('-MockDirectory')]+['-DryRun', '-OutputDirectory', str(root/'dry')]
        assert subprocess.run(dry, capture_output=True).returncode == 0 and not (root/'dry').exists()
        print(f'PASS probe scripts: mock probes-only + coverage/probes, golden rejection, ret keys and first-tick split, {rejected} invalid reports, dry run')


if __name__ == '__main__':
    main()
