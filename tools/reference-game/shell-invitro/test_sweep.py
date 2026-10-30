"""Synthetic transport/diagnostic tests plus opt-in tiny real matrix."""
import json
import os
from pathlib import Path
import struct
import sys
import unittest

import sweep_compare as sweep


class Tests(unittest.TestCase):
    def test_packets(self):
        metadata=dict(output_rng_state=bytes(114).hex(),output_reset_selectors=bytes(60).hex())
        blocks=[json.dumps(metadata).encode(),b'synthetic script',b'text',bytes(2000),b'mec']
        packet=b'ES2P'+b''.join(struct.pack('<I',len(b))+b for b in blocks)
        self.assertEqual(len(sweep.packets(packet*2)),2)
        for at in (1,3,4,5,len(packet)-1):
            with self.assertRaises(ValueError):sweep.packets(packet[:at])
        self.assertEqual(sweep.field_name('mission.var',bytes(2000),1999),'variable[999]')
        self.assertEqual(sweep.field_name('rng',bytes(114),112),'cursor_a')
        mec=struct.pack('<6H',0,1,123,2,42,0)+struct.pack('<34H',0,*([100]*33))+struct.pack('<H',33)+bytes(33)
        self.assertEqual(sweep.field_name('player.mec',mec,8),'actor[0].vehicle')
        self.assertEqual(sweep.signed_word(32768),-32768)
        self.assertEqual(sweep.signed_word(-32769),32767)

    @unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'),'ES2_DATA_ROOT not set')
    def test_tiny_matrix(self):
        cli=sweep.module(CLI,'sweep_portable_cli');cd=Path(os.environ['ES2_DATA_ROOT'])
        rows=sweep.run(cli,CORE,WORKER,cd,sweep.matrix(cli,CORE,cd,[],'tiny'))
        self.assertEqual(sweep.summary(rows),dict(cases=4,equal=3,outside_contract=1,mismatches=0))
        retained=sweep.retained_runs(cli,CORE,WORKER,cd,[],tiny=True)
        self.assertTrue(all(r['equal'] for r in retained),retained)
        initialized=sweep.seed_runs(cli,CORE,WORKER,cd,[],[32768,32769],['GER'])
        self.assertTrue(all(r['equal'] for r in initialized),initialized)

    @unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'),'ES2_DATA_ROOT not set')
    def test_tiny_loadouts(self):
        cli=sweep.module(CLI,'sweep_portable_cli');cd=Path(os.environ['ES2_DATA_ROOT'])
        rows=sweep.loadout_runs(cli,CORE,WORKER,cd,[],tiny=True)
        result=sweep.loadout_summary(rows)
        self.assertEqual((result['cases'],result['mismatches'],result['sav_roundtrip_mismatches']),(6,0,0),rows)
        self.assertEqual(result['requested_team_sizes'],[0,1,2,3])
        defaults=sweep.default_sav_runs(cli,CORE,WORKER,cd,tiny=True)
        self.assertTrue(defaults and all(r['equal'] for r in defaults),defaults)

    def test_emitted_save_packet(self):
        metadata=dict(output_rng_state=bytes(114).hex(),output_reset_selectors=bytes(60).hex(),emitted_save=1)
        blocks=[json.dumps(metadata).encode(),b'script',b'text',bytes(2000),b'mec',b'sav bytes']
        packet=b'ES2P'+b''.join(struct.pack('<I',len(b))+b for b in blocks)
        self.assertEqual(sweep.packets(packet)[0]['sav'],b'sav bytes')
        with self.assertRaises(ValueError):sweep.packets(packet[:-2])


if __name__=='__main__':
    CLI,CORE,WORKER=map(lambda s:Path(s).resolve(),sys.argv[1:4])
    unittest.main(argv=[sys.argv[0]])
