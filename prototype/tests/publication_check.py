# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from publication import fingerprint, publish, PublicationError, recovery_directory
from source_ownership import Ownership, OwnershipError


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
            with patch('publication.os.replace', side_effect=interrupt):
                with self.assertRaisesRegex(PublicationError, 'Save interrupted'):
                    publish(owner, fingerprint(self.image), self.stage,
                            (self.sidecar, fingerprint(self.sidecar), self.stage))
            owner.check()
        journal = recovery_directory(self.image)
        self.assertEqual(self.image.read_bytes(), b'edited')
        self.assertEqual(self.sidecar.read_bytes(), b'original definition')
        self.assertEqual((journal / 'old-0').read_bytes(), b'original image')
        self.assertEqual((journal / 'new-1').read_bytes(), b'edited')

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
