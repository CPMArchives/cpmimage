# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify physical directory placement with a patched standalone mkfs.cpm."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class MkfsAddressingCheck(unittest.TestCase):
    def test_reserved_sectors_offset_and_skewed_metadata(self):
        dispatcher = os.environ.get('CPMIMAGE_DISPATCH_APP')
        formatter = None if dispatcher else Path(os.environ['CPMIMAGE_MKFS']).resolve()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for reservation, offset, skew, plus in (
                ('bootsec 11', 17, '', True),
                ('bootsec 11', 1024, '0,1,2,4,3,5,6,7', True),
                ('boottrk 1', 17, '1,0,2,3,4,5,6,7', True),
                ('boottrk 1\n bootsec 11', 0, '0,1,2,3,4,5,6,7', False),
            ):
                with self.subTest(reservation=reservation, offset=offset):
                    version = '3' if plus else '2.2'
                    skew_line = f' skewtab {skew}\n' if skew else ''
                    (root / 'diskdefs').write_text(
                        f'diskdef test\n seclen 128\n tracks 40\n sectrk 8\n'
                        f' blocksize 1024\n maxdir 32\n {reservation}\n offset {offset}\n'
                        f'{skew_line} os {version}\nend\n')
                    image = root / 'disk.raw'
                    original = b'Z' * (40960 + offset)
                    image.write_bytes(original)
                    (root / 'boot').write_bytes(b'B' * 128)
                    options = ['-L', 'PROOF', '-t'] if plus else []
                    command = [dispatcher, '/mkfs.cpm', '/diskdefs=' + str(root / 'diskdefs')] if dispatcher else [formatter]
                    subprocess.run([*command, '-f', 'test', '-b', 'boot', *options, image],
                                   cwd=root, check=True)
                    reserved = 11 if 'bootsec' in reservation else 8
                    expected = bytearray(original)
                    expected[offset:offset + reserved * 128] = b'B' * 128 + b'\xe5' * (reserved * 128 - 128)
                    actual = image.read_bytes()
                    table = list(map(int, skew.split(','))) if skew else list(range(8))
                    for i in range(8):
                        track, slot = divmod(reserved + i, 8)
                        start = offset + (track * 8 + table[slot]) * 128
                        sector = actual[start:start + 128]
                        if plus:
                            self.assertEqual(sector[96], 0x21)
                            if i == 0:
                                self.assertEqual(sector[:13], b'\x20PROOF      \x11')
                            else:
                                self.assertEqual(sector[:96], b'\xe5' * 96)
                        else:
                            self.assertEqual(sector, b'\xe5' * 128)
                        expected[start:start + 128] = sector
                    self.assertEqual(actual, expected)


if __name__ == '__main__':
    unittest.main()
