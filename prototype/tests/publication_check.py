# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from contextlib import contextmanager

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from publication import fingerprint, publish, PublicationError, recovery_directory
from source_ownership import Ownership, OwnershipError


@contextmanager
def replace_with(operation):
    original = os.replace
    os.replace = operation
    try:
        yield
    finally:
        os.replace = original


class PublicationCheck(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.image = self.root / 'disk.raw'
        self.sidecar = self.root / 'disk.FDF'
        self.stage = self.root / 'edited'
        self.image.write_bytes(b'original image')
        self.sidecar.write_bytes(b'original definition')
        self.stage.write_bytes(b'edited')

    def test_interrupted_sidecar_retains_recovery(self):
        replace = os.replace

        def interrupt(source, target):
            if target == self.sidecar:
                raise OSError('injected sidecar failure')
            replace(source, target)

        with Ownership(self.image, self.root / 'locks') as owner:
            with replace_with(interrupt):
                with self.assertRaisesRegex(PublicationError, 'Save interrupted'):
                    publish(owner, fingerprint(self.image), self.stage,
                            (self.sidecar, fingerprint(self.sidecar), self.stage))
            owner.check()
        journal = recovery_directory(self.image)
        self.assertEqual(self.image.read_bytes(), b'edited')
        self.assertEqual(self.sidecar.read_bytes(), b'original definition')
        self.assertEqual((journal / 'old-0').read_bytes(), b'original image')
        self.assertEqual((journal / 'new-1').read_bytes(), b'edited')

    def test_process_exit_mid_publication_retains_journal_and_releases_lock(self):
        worker = """
import os,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from publication import publish, fingerprint
from source_ownership import Ownership
root=Path(sys.argv[2])
image, sidecar, staged=(root/name for name in ('disk.raw','disk.FDF','edited'))
replace=os.replace
def interrupted(src,dst):
    if dst==sidecar:
        os._exit(73)
    replace(src,dst)
os.replace=interrupted
with Ownership(image,root/'locks') as owner:
    publish(owner,fingerprint(image),staged,(sidecar,fingerprint(sidecar),staged))
"""
        module = str(Path(__file__).resolve().parents[1])
        process = subprocess.run([sys.executable, '-B', '-c', worker, module, str(self.root)], timeout=15)
        self.assertEqual(process.returncode, 73)
        with Ownership(self.image, self.root / 'locks') as owner:
            with self.assertRaisesRegex(PublicationError, 'previous interrupted save'):
                publish(owner, fingerprint(self.image), self.stage)
        journal = recovery_directory(self.image)
        self.assertTrue((journal / 'manifest.json').is_file())
        self.assertEqual((journal / 'old-0').read_bytes(), b'original image')
        self.assertEqual((journal / 'new-1').read_bytes(), b'edited')
        self.assertEqual(self.sidecar.read_bytes(), b'original definition')

    def test_backup_failure_preserves_image(self):
        self.image.with_suffix('.raw.BAK').mkdir()
        with Ownership(self.image, self.root / 'locks') as owner:
            with self.assertRaisesRegex(PublicationError, 'Cannot replace backup'):
                publish(owner, fingerprint(self.image), self.stage)
            owner.check()
        self.assertEqual(self.image.read_bytes(), b'original image')
        self.assertFalse(recovery_directory(self.image).exists())

    def test_multiply_linked_image_requires_separate_destination(self):
        alias = self.root / 'alias.raw'
        os.link(self.image, alias)
        with Ownership(self.image, self.root / 'locks') as owner:
            with self.assertRaises(OwnershipError):
                Ownership(alias, self.root / 'locks')
            with self.assertRaisesRegex(OwnershipError, 'multiple hard links'):
                publish(owner, fingerprint(self.image), self.stage)
        self.assertEqual(alias.read_bytes(), b'original image')


if __name__ == '__main__':
    unittest.main()
