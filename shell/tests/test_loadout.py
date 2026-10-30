"""Loadout front end: synthetic resolution/validation tests, plus opt-in real
proofs against the original preparation (ES2_DATA_ROOT). Temporary files use
Bazel's TEST_TMPDIR (outside every repository); no original bytes are stored."""
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

spec = importlib.util.spec_from_file_location('es2loadout', sys.argv[1])
loadout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(loadout)
CORE = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
WORKER = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
LOADOUT_PATH = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]


class FakeCatalog(loadout.Catalog):
    """Synthetic catalogue: three vehicles, five weapons, invented names."""

    def __init__(self):
        self.vehicles = [(0, 3), (5, 9), (8, 7)]
        self.weapons = {i: dict(cost=0, flag=1, final=0, internal=f'W{i}') for i in (0, 3, 13, 25, 26)}
        self.armory = {3, 13, 25}
        self.hardpoints = {0: {3: [0, 1, 2], 13: [2]}, 1: {3: [0, 1], 25: [7, 8]}, 2: {25: [4, 5], 13: [2, 3]}}
        self.names = {('ENG', 'ESTEXT', 110): 'Alpha', ('ENG', 'ESTEXT', 111): 'Bravo Herc',
                      ('GER', 'ESTEXT', 112): 'Charlie', ('ENG', 'WEAPONS', 3): 'Cannon 50mm',
                      ('FRE', 'WEAPONS', 25): 'Canon Plasma', ('ENG', 'WEAPONS', 13): 'Rack 6X.'}


def base_description():
    pilots = [dict(index=i, name=f'P{i}', variant=i, rank=i % 3) for i in range(36)]
    return dict(pilots=pilots, selection=[0, 2, 4, 1, 3, 5])


class Synthetic(unittest.TestCase):
    def setUp(self):
        self.catalog = FakeCatalog()

    def test_names(self):
        c = self.catalog
        self.assertEqual(c.resolve_herc('bravo  HERC'), (1, 5, 9))
        self.assertEqual(c.resolve_herc('charlie'), (2, 8, 7))
        self.assertEqual(c.resolve_herc(0), (0, 0, 3))
        self.assertEqual(c.resolve_weapon('cannon 50MM'), 3)
        self.assertEqual(c.resolve_weapon('canon plasma'), 25)
        self.assertEqual(c.resolve_weapon('rack 6x'), 13)
        self.assertEqual(c.resolve_weapon('w26'), 26)
        for bad in ('Delta', 7, True):
            with self.assertRaises(ValueError):
                c.resolve_herc(bad)
        for bad in ('Laser', 4, False):
            with self.assertRaises(ValueError):
                c.resolve_weapon(bad)
        self.assertEqual([c.mount_secondary(i) for i in (12, 13, 15, 16)], [5, 1, 1, 5])

    def test_weapon_entries(self):
        self.assertEqual(loadout._weapon_entries([3, None, 0, 'x'], 4), [(0, 3), (3, 'x')])
        self.assertEqual(loadout._weapon_entries({'2': 13, '0': 3}, 3), [(0, 3), (2, 13)])
        for spec, capacity in (([1, 2, 3, 4], 3), ({'3': 1}, 3), ({'-1': 1}, 3), ('bad', 3)):
            with self.assertRaises(ValueError):
                loadout._weapon_entries(spec, capacity)

    def test_resolution_and_rules(self):
        spec = {'mission': 'X', 'player': {'herc': 'Charlie', 'weapons': {'4': 'Canon Plasma', '5': 25, '2': 13}},
                'team': [{'pilot': 'P26', 'herc': 0, 'weapons': [3, 3, {'item': 13, 'secondary': 77}]},
                         {'pilot': {'group': 0}, 'herc': 'Bravo Herc', 'weapons': {'7': 25}}]}
        plan, summary, findings = loadout.resolve(self.catalog, spec, base_description())
        self.assertEqual(findings, [dict(rule='roster-selection', who='team1', detail=
            'selected pilot of group 2 changes from 4 to 2; the next-replacement index is kept')])
        self.assertEqual(plan['player'], (8, [(2, 13, None), (4, 25, None), (5, 25, None)]))
        self.assertEqual(plan['team'][0], (2, 2, 0, [(0, 3, None), (1, 3, None), (2, 13, 77)]))
        self.assertEqual(plan['team'][1][:3], (0, 0, 5))
        self.assertIsNone(plan['flags'])
        self.assertEqual(summary['team1']['weapons'][2]['secondary'], 77)
        self.assertEqual(summary['player']['weapons'][0]['secondary'], 1)
        blob = loadout.plan_blob(plan)
        self.assertEqual(blob[:5], struct.pack('<BBhB', 0, 0, 8, 3))
        self.assertEqual(len(blob), 3 * 5 + 7 * 7)
        # Hangar rules are findings, the CLI decides whether they block.
        spec['player']['weapons'] = {'0': 25, '1': 26}
        _, _, findings = loadout.resolve(self.catalog, spec, base_description())
        self.assertEqual([f['rule'] for f in findings],
                         ['hardpoint', 'armory-list', 'known-dbsim-stop', 'roster-selection'])
        spec['player']['weapons'] = [None, None, 13]
        spec['availability'] = [1] * 33
        spec['availability'][3] = 0
        _, _, findings = loadout.resolve(self.catalog, spec, base_description())
        self.assertEqual([f['rule'] for f in findings], ['roster-selection', 'availability'])

    def test_structural_errors(self):
        good = {'player': {'herc': 0}}
        for change in ({'unknown': 1}, {'schema': 2}, {'player': {}}, {'team': [{}, {}, {}, {}]},
                       {'team': [{'herc': 0, 'pilot': {'group': 3}}]},
                       {'team': [{'herc': 0, 'pilot': {'group': 0, 'index': 12}}]},
                       {'team': [{'herc': 0, 'pilot': {'group': 1}}, {'herc': 0, 'pilot': {'group': 1}}]},
                       {'team': [{'herc': 0, 'pilot': 'nobody'}]}, {'availability': [1] * 32},
                       {'variables': 'keep'}, {'player': {'herc': 0, 'weapons': [{'item': 3, 'secondary': -1}]}},
                       {'player': {'herc': 0, 'weapons': [0, 0, 0, 3]}}):
            with self.assertRaises(ValueError, msg=change):
                loadout.resolve(self.catalog, dict(good, **change), base_description())
        plan, _, _ = loadout.resolve(self.catalog, dict(good, variables='zero', availability=[0] * 33),
                                     base_description())
        self.assertTrue(plan['zero_variables'])
        self.assertEqual(plan['flags'], [0] * 33)

    def test_mec_records(self):
        mec = struct.pack('<HH', 0, 2)
        for vehicle, items in ((8, (25, 0)), (0, (3,))):
            n = len(items)
            mec += struct.pack('<HHhH', 7, 1, vehicle, n) + struct.pack(f'<{n}h', *items)
            mec += struct.pack(f'<{n}H', *([5] * n)) + struct.pack('<34H', 0, *([100] * 33))
        mec += struct.pack('<H', 33) + bytes(33)
        records = loadout.mec_records(mec)
        self.assertEqual([r['vehicle'] for r in records], [8, 0])
        self.assertEqual(records[0]['items'], [25, 0])

    def test_output_refusals(self):
        base = Path(os.environ['TEST_TMPDIR']).resolve()
        checkout = Path(tempfile.mkdtemp(dir=base))
        (checkout / '.git').mkdir()
        (checkout / 'build' / 'installs').mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, 'installs'):
            loadout.checked_output(checkout / 'build' / 'installs' / 'never-created')
        for parent in base.parents:
            if parent.name == 'execroot':
                base = parent.parent
                break
        with tempfile.TemporaryDirectory(prefix='loadout-cli-', dir=base) as temp:
            with self.assertRaisesRegex(ValueError, 'lowercase'):
                loadout.checked_output(Path(temp) / 'Upper')
            with self.assertRaisesRegex(ValueError, 'parent'):
                loadout.checked_output(Path(temp) / 'missing' / 'child')
            self.assertEqual(loadout.checked_output(Path(temp) / 'ok-name').name, 'ok-name')


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT') and CORE and WORKER, 'ES2_DATA_ROOT not set')
class Real(unittest.TestCase):
    """Generated SAVs proved against the mapped original preparation."""

    def run_cli(self, spec, *extra):
        base = Path(os.environ['TEST_TMPDIR']).resolve()
        for parent in base.parents:
            if parent.name == 'execroot':
                base = parent.parent
                break
        with tempfile.TemporaryDirectory(prefix='loadout-real-', dir=base) as temp:
            path = Path(temp) / 'loadout.json'
            path.write_bytes(json.dumps(spec).encode())
            report = Path(temp) / 'report.json'
            out, err = io.StringIO(), io.StringIO()
            args = ['--core', str(CORE), '--worker', str(WORKER), '--cd-root', os.environ['ES2_DATA_ROOT'],
                    '--loadout', str(path), '--report', str(report), *extra]
            with redirect_stdout(out), redirect_stderr(err):
                try:
                    code = loadout.main(args)
                except ValueError as error:
                    return 2, str(error), None, None
            sealed = None
            if '--output' in extra:
                directory = Path(extra[extra.index('--output') + 1])
                sealed = json.loads((directory / 'manifest.json').read_text())
            return code, out.getvalue(), json.loads(report.read_text()), sealed

    def test_proofs(self):
        omicron = {'mission': 'C3_09', 'player': {'herc': 'Ogre', 'weapons': {'0': 5, '1': 12, '4': 25}},
                   'team': [{'pilot': {'group': 0}, 'herc': 'Apocalypse', 'weapons': [4, 11]},
                            {'pilot': {'group': 1, 'index': 3}, 'herc': 'Samson', 'weapons': [3, 10]},
                            {'pilot': {'group': 2}, 'herc': 'Colossus', 'weapons': [2, 9]}]}
        code, _, report, _ = self.run_cli(omicron)
        self.assertEqual(code, 0)
        self.assertTrue(report['equal'], report)
        self.assertEqual([r['vehicle'] for r in report['mec']], [6, 5, 3, 4])
        razor = {'mission': 'C4_08', 'language': 'ENG', 'seed': 127, 'variables': 'zero',
                 'player': {'herc': 'Razor', 'weapons': {'4': 'Plasma Cannon', '5': 'Plasma Cannon'}},
                 'team': [{'herc': 'Ogre', 'weapons': [5]}]}
        code, _, report, _ = self.run_cli(razor)
        self.assertEqual(code, 0)
        self.assertTrue(report['equal'], report)
        self.assertEqual(report['world'], 6)
        self.assertEqual([f['rule'] for f in report['findings']], ['team-capacity'])
        # GAME_R.SAV comes from the hash-pinned reference table (any measurement
        # clone holding the pinned bytes), copied into scratch: the clone itself
        # is shared with live oracle regressions.
        pins_path = LOADOUT_PATH.parents[2] / 'tools/reference-game/shell-invitro/reference_pins.py'
        pins_spec = importlib.util.spec_from_file_location('reference_pins', pins_path)
        pins = importlib.util.module_from_spec(pins_spec)
        pins_spec.loader.exec_module(pins)
        save, _ = pins.require(os.environ['ES2_DATA_ROOT'], 'SAV/GAME_R.SAV')
        with tempfile.TemporaryDirectory(prefix='loadout-base-', dir=os.environ['TEST_TMPDIR']) as scratch:
            base = Path(scratch) / 'GAME_R.SAV'
            base.write_bytes(save)
            code, _, report, _ = self.run_cli({'campaign': [1, 0], 'player': {'herc': 0, 'weapons': [3, 3, 7]}},
                                              '--base-sav', str(base))
            self.assertEqual(code, 0)
            self.assertTrue(report['equal'], report)
        code, message, _, _ = self.run_cli({'mission': 'C3_01', 'player': {'herc': 'Ogre', 'weapons': {'1': 25}}})
        self.assertEqual(code, 2)
        self.assertIn('hardpoint', message)
        code, message, _, _ = self.run_cli({'mission': 'TRAIN1', 'player': {'herc': 0}})
        self.assertEqual(code, 2)
        self.assertIn('mode 0', message)

    def test_sealed_launch(self):
        base = Path(os.environ['TEST_TMPDIR']).resolve()
        for parent in base.parents:
            if parent.name == 'execroot':
                base = parent.parent
                break
        with tempfile.TemporaryDirectory(prefix='loadout-seal-', dir=base) as temp:
            destination = Path(temp) / 'synthetic-loadout'
            spec = {'mission': 'C1_01', 'player': {'herc': 0, 'weapons': [2, 2, 18]}}
            code, _, report, manifest = self.run_cli(spec, '--output', str(destination))
            self.assertEqual(code, 0)
            self.assertEqual(manifest['schema'], 1)
            self.assertEqual([f['file'] for f in manifest['files']],
                             ['script.dat', 'MISSION.STR', 'mission.var', 'player.mec'])
            proof = manifest['provenance']['original_proof']
            self.assertTrue(all(proof['checks'].values()))
            sav = (destination / 'loadout.sav').read_bytes()
            self.assertEqual(loadout.sha(sav), manifest['provenance']['generated_sav_sha256'])
            self.assertEqual(loadout.sha((destination / 'loadout.json').read_bytes()),
                             manifest['provenance']['loadout_json_sha256'])
            for entry in manifest['files']:
                self.assertEqual(loadout.sha((destination / entry['file']).read_bytes()), entry['sha256'])
            self.assertEqual(sorted(p.name for p in Path(temp).iterdir()), ['synthetic-loadout'])
            code, message, _, _ = self.run_cli(spec, '--output', str(destination))
            self.assertEqual(code, 2)


if __name__ == '__main__':
    unittest.main()
