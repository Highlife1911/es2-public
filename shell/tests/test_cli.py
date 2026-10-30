"""Synthetic CLI/state/manifest tests. Temporary files use Bazel TEST_TMPDIR."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('es2prep', sys.argv[1])
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)
sys.argv = sys.argv[:1]


class CliTests(unittest.TestCase):
    def test_state_validation(self):
        player = cli.encode_loadout({'actors': [{'vehicle': 2, 'items': [3, 4]}]})
        self.assertEqual(len(player), 123)
        rng = bytes(112) + bytes([24, 55])
        valid = dict(rng=rng, player=player)
        self.assertEqual(len(cli.request(**valid).splitlines()), 4)
        controls=cli.request(rng=rng,save=b'synthetic SAV',tables=bytes(740),
                             training_vehicle=8,preset_flag=255,preset_index=-32768).splitlines()
        self.assertEqual(controls[0].split()[11:],[ '2','8','255','-32768','0','0'])
        self.assertEqual(bytes.fromhex(controls[2]),b'synthetic SAV')
        for change in ({'mission': '../bad'}, {'bank': bytes(1999)}, {'rng': bytes(113)},
                       {'rng': bytes(112) + b'\x38\0'}, {'selectors': bytes(58)},
                       {'group': 32768}, {'mode0_values': [0, 0, 0, 256]}, {'language': 'xx'}):
            with self.assertRaises(ValueError):
                cli.request(**dict(valid, **change))
        for load in ({'actors': []}, {'actors': [{'vehicle': 0, 'items': [0]*11}]},
                     {'actors': [{'vehicle': 0, 'condition': [100]}]},
                     {'actors': [{'vehicle': 0, 'secondary': [5]}]},
                     {'actors': [{'vehicle': 0}], 'flags': [0]}):
            with self.assertRaises(ValueError):
                cli.encode_loadout(load)

    def test_exports(self):
        base = Path(os.environ['TEST_TMPDIR']).resolve()
        # Bazel's execroot contains a .git link. Use its owning output cache
        # for the successful synthetic export; repository refusal stays active.
        for parent in base.parents:
            if parent.name == 'execroot':
                base = parent.parent
                break
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='shell-cli-', dir=base) as temp:
            root = Path(temp)
            fake_repo = root / 'repository'
            fake_repo.mkdir()
            (fake_repo / '.git').write_text('gitdir: synthetic-worktree\n', encoding='ascii')
            with self.assertRaisesRegex(ValueError, 'repository'):
                cli.export_path(fake_repo / 'a' / 'b')
            with self.assertRaisesRegex(ValueError, 'installs'):
                cli.export_path(fake_repo / 'build' / 'installs' / 'never-created')
            buffers = {name: bytes(range(16)) for name in cli.FILES}
            buffers.update(rng=bytes(114), selectors=bytes(60))
            destination = root / 'synthetic-launch'
            manifest = cli.export(buffers, destination, {'kind': 'synthetic-test', 'process': None})
            self.assertEqual(manifest['schema'], 1)
            self.assertEqual(len(manifest['files']), 4)
            self.assertEqual(json.loads((destination / 'manifest.json').read_text()), manifest)
            for entry in manifest['files']:
                data = (destination / entry['file']).read_bytes()
                self.assertEqual(entry['size'], len(data))
                self.assertEqual(entry['sha256'], cli.sha(data))
            with self.assertRaisesRegex(ValueError, 'new immutable'):
                cli.export_path(destination)
            self.assertFalse((fake_repo / 'a').exists())


if __name__ == '__main__':
    unittest.main()
