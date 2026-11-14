# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the catalogue and sidecar protocol through the actual helper."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(os.environ['CPMIMAGE_ROOT']).resolve()
HELPER = Path(os.environ['CPMIMAGE_HELPER']).resolve()
CUSTOM = '''diskdef custom-3740
 seclen 128
 tracks 77
 sectrk 26
 blocksize 1024
 maxdir 64
 skew 6
 bootsec 52
 os 2.2
end
'''


class CatalogueCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.image = self.directory / 'Disk ü.raw'
        shutil.copyfile(ROOT / 'fixtures/ibm3740-independent.raw', self.image)
        self.broker = subprocess.Popen([sys.executable, '-I', '-B', '-u', str(ROOT / 'prototype/broker.py'),
            '--root', str(ROOT), '--helper', str(HELPER)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, text=True)

    def tearDown(self):
        self.broker.stdin.close()
        self.broker.wait(timeout=20)
        self.broker.stdout.close()

    def request(self, op, **values):
        self.broker.stdin.write(json.dumps(dict(v=1, id='test', op=op, **values)) + '\n')
        self.broker.stdin.flush()
        line = self.broker.stdout.readline()
        self.assertTrue(line, 'Broker exited without a reply')
        return json.loads(line)

    def test_bundled_and_reduced_catalogues(self):
        bundled = self.request('catalogue')
        self.assertTrue(bundled['ok'], bundled)
        formats = {row['id']: row for row in bundled['result']['formats']}
        self.assertEqual(len(formats), 148)
        self.assertEqual(formats['bettercpm-default']['family'], 'BetterCP/M')
        self.assertEqual(formats['apple-do']['manufacturer'], 'Apple')
        self.assertEqual(formats['memotech-type1F']['geometry']['offset'], 56 * 1024 * 1024)
        path = self.directory / 'custom ü diskdefs'
        path.write_text(CUSTOM)
        custom = self.request('catalogue', path=str(path))
        self.assertTrue(custom['ok'], custom)
        self.assertEqual([row['id'] for row in custom['result']['formats']], ['custom-3740'])
        rejected = self.request('open', image=str(self.image), format='ibm-3740', container='raw')
        self.assertEqual(rejected['error'], 'unknown_format')
        opened = self.request('open', image=str(self.image), format='custom-3740', container='raw')
        self.assertTrue(opened['ok'], opened)
        path.write_text(CUSTOM.replace('blocksize 1024', 'blocksize 0'))
        self.assertEqual(self.request('catalogue', path=str(path))['error'], 'invalid_definition')
        output = self.directory / 'retained.bin'
        exported = self.request('export', session=opened['result']['session'], name='00BINARY.BIN', destination=str(output))
        self.assertTrue(exported['ok'], exported)

    def test_save_as_publishes_standalone_sidecar(self):
        original = self.image.read_bytes()
        opened = self.request('open', image=str(self.image), format='ibm-3740', container='raw')
        self.assertTrue(opened['ok'], opened)
        destination = self.directory / 'copy.raw'
        options = dict(session=opened['result']['session'], destination=str(destination))
        planned = self.request('save_plan', **options)
        self.assertTrue(planned['ok'], planned)
        declined = self.request('save', plan=planned['result'], **options)
        self.assertEqual(declined['error'], 'confirmation_required')
        self.assertFalse(destination.exists())
        saved = self.request('save', plan=planned['result'], confirm_sidecar=True, **options)
        self.assertTrue(saved['ok'], saved)
        self.assertEqual(destination.read_bytes(), original)
        self.assertEqual(self.image.read_bytes(), original)
        self.assertTrue(destination.with_suffix('.FDF').read_text().startswith('diskdef ibm-3740'))
        reopened = self.request('inspect', image=str(destination))
        self.assertEqual(reopened['result']['sidecar']['id'], 'ibm-3740')

    def test_candidate_ranking_uses_directory_evidence(self):
        selected = self.request('catalogue')
        self.assertTrue(selected['ok'], selected)
        result = self.request('inspect', image=str(self.image))
        self.assertTrue(result['ok'], result)
        candidates = result['result']['candidates']
        self.assertIn('ibm-3740', [row['id'] for row in candidates])
        ranking = [(row['evidence'] == 'Geometry and empty directory', row['id']) for row in candidates]
        self.assertEqual(ranking, sorted(ranking))

    def test_fdf_is_discovered_and_bound_to_content(self):
        path = self.image.with_suffix('.FDF')
        path.write_text(CUSTOM)
        inspected = self.request('inspect', image=str(self.image))
        self.assertTrue(inspected['ok'], inspected)
        sidecar = inspected['result']['sidecar']
        self.assertEqual(sidecar['id'], 'custom-3740')
        self.assertEqual(sidecar['definition_sha256'], hashlib.sha256(CUSTOM.encode()).hexdigest())
        opened = self.request('open', image=str(self.image), format='custom-3740', container='raw',
                              sidecar=True, definition_sha256=sidecar['definition_sha256'])
        self.assertTrue(opened['ok'], opened)
        self.assertEqual(path.read_text(), CUSTOM)
        path.write_text(CUSTOM.replace('skew 6', 'skew 1'))
        changed = self.request('open', image=str(self.image), format='custom-3740', container='raw',
                               sidecar=True, definition_sha256=sidecar['definition_sha256'])
        self.assertEqual(changed['error'], 'definition_changed')
        path.write_text('invalid FDF')
        invalid = self.request('inspect', image=str(self.image))
        self.assertTrue(invalid['ok'], invalid)
        self.assertIn('Disk ü.FDF', invalid['result']['sidecar_warning'])
        self.assertNotIn('sidecar', invalid['result'])

    def test_explicit_choice_has_precedence(self):
        self.image.with_suffix('.fdf').write_text(CUSTOM)
        inspected = self.request('inspect', image=str(self.image), explicit_format='ibm-3740')
        self.assertTrue(inspected['ok'], inspected)
        self.assertNotIn('sidecar', inspected['result'])
        fingerprint = inspected['result']['source_sha256']
        self.image.write_bytes(self.image.read_bytes()[:-1] + b'!')
        changed = self.request('open', image=str(self.image), format='ibm-3740', container='raw',
                               source_sha256=fingerprint)
        self.assertEqual(changed['error'], 'source_changed')


if __name__ == '__main__':
    unittest.main(verbosity=2)
