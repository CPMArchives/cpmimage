# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke-test the staged dispatcher against the bundled utility binaries."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

APP = Path(os.environ['CPMIMAGE_DISPATCH_APP']).resolve()
ROOT = Path(os.environ['CPMIMAGE_ROOT']).resolve()
UTILITIES = ('cpmls', 'cpmrm', 'cpmcp', 'cpmchmod', 'cpmchattr',
             'mkfs.cpm', 'fsck.cpm', 'fsed.cpm')


class DispatchCheck(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.cwd = Path(temp.name)
        self.catalogue = self.cwd / 'suite diskdefs'
        shutil.copyfile(ROOT / 'fixtures/diskdefs', self.catalogue)
        (self.cwd / 'diskdefs').write_text('invalid local catalogue')
        self.image = self.cwd / 'disk image.raw'
        shutil.copyfile(ROOT / 'fixtures/ibm3740-independent.raw', self.image)
        self.env = dict(os.environ, QT_QPA_PLATFORM='missing-platform',
                        CPMTOOLS_DISKDEFS=str(self.catalogue))

    def run_command(self, executable, *args, data=b''):
        return subprocess.run([executable, *args], cwd=self.cwd, env=self.env,
                              input=data, capture_output=True, timeout=10)

    def test_listing_and_exit_status(self):
        direct = APP.parent / 'cpmtools' / ('cpmls.exe' if os.name == 'nt' else 'cpmls')
        for args in [('-f', 'ibm-3740', self.image.name),
                     ('-f', 'ibm-3740', 'missing.raw')]:
            with self.subTest(args=args):
                expected = self.run_command(direct, *args)
                actual = self.run_command(APP, args[0], '/cpmls', *args[1:],
                                          '/diskdefs=' + str(self.catalogue))
                self.assertEqual((actual.returncode, actual.stdout, actual.stderr),
                                 (expected.returncode, expected.stdout, expected.stderr))
        self.assertIn(b'binary.bin', self.run_command(APP, '/cpmls', '-f',
                      'ibm-3740', self.image.name, '--diskdefs', str(self.catalogue)).stdout.lower())

    def test_cpmcp_preserves_relative_paths_and_payload(self):
        result = self.run_command(APP, '-f', 'ibm-3740', self.image.name,
                                  '/cpmcp', '0:binary.bin', 'export with spaces.bin')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(hashlib.sha256((self.cwd / 'export with spaces.bin').read_bytes()).hexdigest(),
                         '110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b')

    def test_selector_errors_precede_gui_startup(self):
        for args in [('--image', self.image.name, '/cpmls'), ('/cpmcp', '/cpmls'),
                     ('/cpmls', '/diskdefs='), ('/cpmls', '/diskdefs=missing'),
                     ('/cpmls', '/diskdefs=a', '/diskdefs=b')]:
            with self.subTest(args=args):
                result = self.run_command(APP, *args)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn(b'cpmimage:', result.stderr)

    def test_all_selectors_reach_the_bundled_utilities(self):
        for utility in UTILITIES:
            with self.subTest(utility=utility):
                direct = APP.parent / 'cpmtools' / (utility + ('.exe' if os.name == 'nt' else ''))
                expected = self.run_command(direct, '-?')
                actual = self.run_command(APP, '-?', '/' + utility,
                                          '/diskdefs=' + str(self.catalogue))
                self.assertEqual((actual.returncode, actual.stdout, actual.stderr),
                                 (expected.returncode, expected.stdout, expected.stderr))
