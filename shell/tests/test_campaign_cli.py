"""es2campaign.py shell options: synthetic prefs images through the
private core transport, option resolution and argument rules; plus an opt-in
real check (ES2_DATA_ROOT) of the CD DATA/PREFS.CFG and of the file VSHELL
would write for the default options. Temporary files use Bazel's TEST_TMPDIR
(outside every repository); no original bytes are stored."""
import hashlib
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

spec = importlib.util.spec_from_file_location('es2campaign', sys.argv[1])
campaign = importlib.util.module_from_spec(spec)
spec.loader.exec_module(campaign)
CORE = Path(sys.argv[2]).resolve()
LOADOUT = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
sys.argv = sys.argv[:1]
MODE, REPAIR, BUILD = 0x2a, 0x2c, 0x2d
# SHA-256 of the CD file, as listed in data/iso_file_hashes.sha256.
CD_PREFS_SHA256 = '01fd1750494d081c8aba6207f70100f2dc04d632b25879fc345e50382453ed4b'


def invented(fill=0x40):
    """An invented 54-byte image: every byte differs from the option values."""
    return bytes((fill + i) & 0xff for i in range(54))


def edited(base, **values):
    out = bytearray(base)
    for name, offset in (('mode', MODE), ('repair', REPAIR), ('build', BUILD)):
        if name in values:
            out[offset] = values[name]
    return bytes(out)


def scratch():
    base = Path(os.environ.get('TEST_TMPDIR', tempfile.gettempdir())).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            base = parent.parent
            break
    return tempfile.TemporaryDirectory(prefix='campaign-cli-', dir=base)


class Synthetic(unittest.TestCase):
    def test_read_as_vshell_starts(self):
        image = invented()
        out = campaign.prefs_request(CORE, image)
        self.assertEqual((out['mode'], out['repair'], out['restock']), (image[MODE], image[REPAIR], image[BUILD]))
        self.assertEqual(out['prefs'], image)
        # the mode word is the zero-extended byte, not a signed one
        self.assertEqual(campaign.prefs_request(CORE, edited(image, mode=0xff))['mode'], 255)
        # missing file: zero block; short file: zero padded; long file: first 54 bytes
        missing = campaign.prefs_request(CORE, None)
        self.assertEqual(missing['prefs'], bytes(54))
        self.assertEqual((missing['mode'], missing['repair'], missing['restock']), (0, 0, 0))
        self.assertEqual(campaign.prefs_request(CORE, image[:5])['prefs'], image[:5] + bytes(49))
        self.assertEqual(campaign.prefs_request(CORE, image + b'xyz')['prefs'], image)
        self.assertEqual(campaign.prefs_request(CORE, b'')['prefs'], bytes(54))

    def test_ui_choices(self):
        image = invented()
        for entry, mode in campaign.MENU_ENTRIES.items():
            for repair, r in campaign.REPAIR_OPTIONS.items():
                for build, b in campaign.BUILD_OPTIONS.items():
                    out = campaign.prefs_request(CORE, image, mode=mode, repair=r, build=b)
                    self.assertEqual(out['prefs'], edited(image, mode=mode, repair=r, build=b), (entry, repair, build))
                    self.assertEqual((out['mode'], out['repair'], out['restock']), (mode, r, b))
        self.assertEqual(campaign.prefs_request(CORE, image, repair=2)['prefs'], edited(image, repair=2))
        for bad in (dict(mode=2), dict(mode=-2), dict(repair=3), dict(build=2), dict(build=-5), dict(mode=256)):
            with self.assertRaises(ValueError):
                campaign.prefs_request(CORE, image, **bad)
        with self.assertRaises(ValueError):
            campaign.prefs_request(CORE, 'text')

    def test_menu_and_labels(self):
        self.assertEqual({k for k, v in campaign.MENU_ENTRIES.items() if v == 1},
                         {'new-game', 'continue-game', 'save-restore'})
        self.assertEqual({k for k, v in campaign.MENU_ENTRIES.items() if v == 0}, {'instant-action', 'practice-missions'})
        self.assertEqual(campaign.REPAIR_OPTIONS, {'auto-all': 0, 'manual-mine': 1, 'manual-all': 2})
        self.assertEqual(campaign.BUILD_OPTIONS, {'auto': 0, 'manual': 1})

    def test_resolution(self):
        root = Path('no-such-cd-root')
        # no prefs, no UI choice: defaults, no core call and no CD read
        options, block = campaign.resolve_options(CORE, root)
        self.assertEqual(options, dict(mode=1, repair=0, restock=0, source='defaults'))
        self.assertIsNone(block)
        options, _ = campaign.resolve_options(CORE, root, raw_mode=0, raw_restock=7)
        self.assertEqual(options, dict(mode=0, repair=0, restock=7, source='defaults+raw'))
        # a prefs file supplies all three; UI choices change only their bytes
        image = edited(invented(), mode=1, repair=2, build=1)
        options, block = campaign.resolve_options(CORE, root, prefs=image)
        self.assertEqual((options['mode'], options['repair'], options['restock'], options['source']), (1, 2, 1, 'prefs'))
        self.assertEqual(block, image)
        options, block = campaign.resolve_options(CORE, root, prefs=image, menu_entry='practice-missions',
                                                  repair='manual-mine')
        self.assertEqual((options['mode'], options['repair'], options['restock']), (0, 1, 1))
        self.assertEqual(block, edited(image, mode=0, repair=1))
        options, _ = campaign.resolve_options(CORE, root, prefs=image, raw_repair=9)
        self.assertEqual((options['repair'], options['source']), (9, 'prefs+raw'))
        with self.assertRaises(ValueError):
            campaign.resolve_options(CORE, root, prefs=image, raw_mode=1, need_block=True)
        # without --prefs a UI choice starts from the CD file, which is absent here
        with self.assertRaises(OSError):
            campaign.resolve_options(CORE, root, repair='auto-all')

    def test_arguments(self):
        common = ['--core', str(CORE), '--cd-root', 'x', '--save', 'x', '--mission-var', 'x', '--results', 'x']
        for extra in (['--repair', 'auto-all', '--repair-option', '0'],
                      ['--weapons-building', 'auto', '--restock-option', '1'],
                      ['--menu-entry', 'new-game', '--mode', '1'],
                      ['--repair', 'sometimes'], ['--menu-entry', 'campaign']):
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                campaign.main(common + extra)
            self.assertEqual(caught.exception.code, 2)


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT') and LOADOUT, 'ES2_DATA_ROOT not set')
class Real(unittest.TestCase):
    """The CD prefs file and what VSHELL would write for the default options."""

    @classmethod
    def setUpClass(cls):
        cls.root = Path(os.environ['ES2_DATA_ROOT'])
        cls.cd = (cls.root / 'DATA' / 'PREFS.CFG').read_bytes()

    def test_cd_prefs(self):
        self.assertEqual(hashlib.sha256(self.cd).hexdigest(), CD_PREFS_SHA256)
        self.assertEqual(len(self.cd), 54)
        out = campaign.prefs_request(CORE, self.cd)
        # a fresh installation: single-mission mode, auto repair, auto build
        self.assertEqual((out['mode'], out['repair'], out['restock']), (0, 0, 0))
        self.assertEqual(out['prefs'], self.cd)

    def test_default_file_byte_for_byte(self):
        # VSHELL after a campaign entry with untouched Preferences: the CD
        # file with byte +0x2a = 1 (0x40ec56 rewrites that byte only)
        expected = self.cd[:MODE] + b'\x01' + self.cd[MODE + 1:]
        options, block = campaign.resolve_options(CORE, self.root, need_block=True)
        self.assertEqual(block, expected)
        self.assertEqual((options['mode'], options['repair'], options['restock']), (1, 0, 0))
        defaults, _ = campaign.resolve_options(CORE, self.root)
        self.assertEqual((defaults['mode'], defaults['repair'], defaults['restock']), (1, 0, 0))
        for entry, mode in campaign.MENU_ENTRIES.items():
            for repair, r in campaign.REPAIR_OPTIONS.items():
                for build, b in campaign.BUILD_OPTIONS.items():
                    _, block = campaign.resolve_options(CORE, self.root, menu_entry=entry, repair=repair, build=build)
                    self.assertEqual(block, edited(self.cd, mode=mode, repair=r, build=b))

    def test_cli_defaults_equal_written_prefs(self):
        spec = importlib.util.spec_from_file_location('es2loadout_for_campaign_test', LOADOUT)
        loadout = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(loadout)
        catalog = loadout.Catalog(CORE, self.root)
        sav = loadout.default_save(CORE, self.root, catalog, 'GER', 1, 0)
        with scratch() as temp:
            temp = Path(temp)
            (temp / 'game.sav').write_bytes(sav)
            (temp / 'mission.var').write_bytes(bytes(2000))
            # outcome 1, tally 5000, no pairs; then per consumed pilot 33
            # condition words of 100 and three counters (trailing bytes are
            # ignored, so four records cover any team)
            pilot = struct.pack('<33h', *[100] * 33) + struct.pack('<3h', 1, 2, 3)
            (temp / 'results.dat').write_bytes(struct.pack('<hIh', 1, 5000, 0) + pilot * 4)
            common = ['--core', str(CORE), '--cd-root', str(self.root), '--save', str(temp / 'game.sav'),
                      '--mission-var', str(temp / 'mission.var'), '--results', str(temp / 'results.dat')]

            def run(*extra):
                out = io.StringIO()
                with redirect_stdout(out):
                    code = campaign.main(common + list(extra))
                return code, json.loads(out.getvalue())

            code, plain = run()
            self.assertEqual(code, 0, plain)
            self.assertEqual(plain['options'], dict(mode=1, repair_option=0, restock_option=0, source='defaults'))
            self.assertEqual(plain['ending'], 'advanced')
            code, written = run('--output-prefs', str(temp / 'prefs.cfg'))
            self.assertEqual(code, 0, written)
            self.assertEqual((temp / 'prefs.cfg').read_bytes(), self.cd[:MODE] + b'\x01' + self.cd[MODE + 1:])
            code, restarted = run('--prefs', str(temp / 'prefs.cfg'))
            self.assertEqual(code, 0, restarted)
            self.assertEqual(restarted['options']['source'], 'prefs')
            code, named = run('--menu-entry', 'continue-game', '--repair', 'auto-all', '--weapons-building', 'auto')
            self.assertEqual(code, 0, named)
            for result in (written, restarted, named):
                self.assertEqual({k: v for k, v in result.items() if k != 'options'},
                                 {k: v for k, v in plain.items() if k != 'options'})
            # a single-mission entry ends the results path before advancing
            code, single = run('--menu-entry', 'practice-missions')
            self.assertEqual((code, single['ending'], single['options']['mode']), (0, 'other-mode', 0))


if __name__ == '__main__':
    unittest.main(verbosity=2)
