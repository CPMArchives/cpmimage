# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare native reader results against fixtures made by unmodified cpmtools."""
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
TOOLS = Path(os.environ['CPMIMAGE_UPSTREAM_TOOLS']).resolve()


class DiskdefsCheck(unittest.TestCase):
    def test_representative_upstream_images(self):
        formats = (
            'apple-do', '4mb-hd', 'pcw', 'yaze512', 'com8',
            'memotech-type1F', 'amp1', 'z80pack-hd',
        )
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            shutil.copyfile(ROOT / 'fixtures/diskdefs', directory / 'diskdefs')
            original = bytes(range(256)) * 19
            payload = directory / 'payload'
            payload.write_bytes(original)
            with subprocess.Popen([sys.executable, '-B', str(ROOT / 'prototype/broker.py'),
                    '--root', str(ROOT), '--helper', str(HELPER)], stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE, text=True) as broker:
                def request(**values):
                    broker.stdin.write(json.dumps(dict(v=1, id='oracle', **values)) + '\n')
                    broker.stdin.flush()
                    return json.loads(broker.stdout.readline())

                try:
                    for fmt in formats:
                        with self.subTest(format=fmt):
                            image = directory / (fmt + '.raw')
                            subprocess.run([TOOLS / 'mkfs.cpm', '-f', fmt, image], cwd=directory, check=True)
                            description = json.loads(subprocess.check_output([HELPER, 'describe', fmt], cwd=directory))
                            full_size = description['offset'] + description['sector_size'] * description['tracks'] * description['sectors_per_track']
                            # mkfs.cpm writes the filesystem region from byte zero.
                            # Place it at the catalogue's offset before using cpmcp.
                            if description['offset']:
                                filesystem = image.read_bytes()
                                with image.open('wb') as stream:
                                    stream.seek(description['offset'])
                                    stream.write(filesystem)
                            with image.open('ab') as stream:
                                stream.write(b'\xe5' * (full_size - image.stat().st_size))
                            subprocess.run([TOOLS / 'cpmcp', '-f', fmt, image, payload, '0:CHECK.BIN'], cwd=directory, check=True)
                            fingerprint = hashlib.sha256(image.read_bytes()).hexdigest()
                            reference = directory / 'reference'
                            subprocess.run([TOOLS / 'cpmcp', '-f', fmt, image, '0:CHECK.BIN', reference], cwd=directory, check=True)
                            result = request(op='open', image=str(image), format=fmt, container='raw')
                            self.assertTrue(result['ok'], result)
                            self.assertIn('00CHECK.BIN', {e['name'] for e in result['result']['entries']})
                            destination = directory / 'export'
                            exported = request(op='export', session=result['result']['session'],
                                               name='00CHECK.BIN', destination=str(destination))
                            self.assertTrue(exported['ok'], exported)
                            self.assertEqual(destination.read_bytes(), reference.read_bytes())
                            self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(), fingerprint)
                            destination.unlink()
                            reference.unlink()
                            image.unlink()
                finally:
                    broker.stdin.close()

    def test_invalid_definition(self):
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp) / 'diskdefs').write_text('diskdef broken\n seclen 0\nend\n')
            result = subprocess.run([HELPER, 'describe', 'broken'], cwd=temp, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b'invalid disk definition', result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
