# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from container_mapping import MappingError, PhysicalMapping, Track, inspect_imd


class ContainerMappingCheck(unittest.TestCase):
    def test_inventory_preserves_physical_order_and_comment(self):
        tracks, comment = inspect_imd(b'IMD header\nComment\x1a'
                                      + bytes((5, 0, 0, 2, 0, 2, 1, 2, 65, 2, 66)))
        self.assertEqual(comment, b'Comment')
        self.assertEqual(tracks[0, 0].sectors, (2, 1))

    def test_duplicate_missing_and_truncated_imd_sectors(self):
        prefix = b'IMD header\x1a' + bytes((5, 0, 0, 2, 0))
        for tail, error in ((bytes((1, 1)), 'Duplicate'),
                            (bytes((2, 1, 0)), 'missing'),
                            (bytes((2, 1, 1, 0)), 'Truncated')):
            with self.subTest(error=error), self.assertRaisesRegex(MappingError, error):
                inspect_imd(prefix + tail)

    def test_mapping_checks_geometry_without_applying_skew(self):
        geometry = dict(tracks=2, offset=0, sector_size=128, sectors_per_track=2, skew=2)
        tracks = (Track(0, 1, 5, 128, (1, 2)), Track(0, 0, 2, 128, (9, 8)))
        PhysicalMapping(tracks).verify(geometry)
        with self.assertRaisesRegex(MappingError, 'track address'):
            PhysicalMapping((tracks[0], tracks[0])).verify(geometry)


if __name__ == '__main__':
    unittest.main()
