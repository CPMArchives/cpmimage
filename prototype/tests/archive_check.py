# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from archive_view import crc_arc, inspect


def squeezed():
    # Huffman leaves for 'A' and EOF, then bits 0, 1.
    return b'\x76\xffA\0A.TXT\0' + struct.pack('<hhhB', 1, -66, -257, 2)


def crunched():
    # 9-bit MSB-first literal 'B' and EOF, then the 16-bit byte sum.
    codes = ((66 << 9) | 256) << 6
    return b'\x76\xfeB.TXT\0\x20\x20\0\0' + codes.to_bytes(3, 'big') + b'B\0'


def library(name, content, *additional):
    header = bytearray(128)
    header[1:12] = b' ' * 11
    header[14] = 1
    data = bytearray()
    members = ((name, content), *additional)
    for slot, (filename, payload) in enumerate(members, 1):
        stem, suffix = filename.split('.')
        entry = 32 * slot
        header[entry + 1:entry + 12] = stem.encode().ljust(8) + suffix.encode().ljust(3)
        sectors = (len(payload) + 127) // 128
        struct.pack_into('<HH', header, entry + 12, 1 + len(data) // 128, sectors)
        header[entry + 26] = sectors * 128 - len(payload)
        data.extend(payload.ljust(sectors * 128, b'\x1a'))
    for slot in range(len(members) + 1, 4):
        header[slot * 32] = 255
    return bytes(header + data)


def ark(name, content):
    return (b'\x1a\x02' + name.encode().ljust(13, b'\0') +
            struct.pack('<IIHI', len(content), 0, crc_arc(content), len(content)) +
            content + b'\x1a\0')


class ArchiveCheck(unittest.TestCase):
    def test_sq_cr_auto_and_forced(self):
        for mode, data, expected in [('sq', squeezed(), b'A'), ('cr', crunched(), b'B')]:
            for selector in ('auto', mode):
                result, original, decoded = inspect(data, 'UNKNOWN.BIN', selector)
                self.assertFalse(result['notice'])
                self.assertEqual(decoded, expected)
                self.assertEqual(original, data)
            for bad in (data[:-1], data[:-1] + b'\xff'):
                self.assertIsNone(inspect(bad, 'UNKNOWN.BIN', mode)[2])
        for mode in ('sq', 'cr'):
            result, original, decoded = inspect(b'plain text', 'NOTE.TXT', mode)
            self.assertTrue(result['notice'])
            self.assertEqual(original, b'plain text')
            self.assertIsNone(decoded)

    def test_archive_members_and_nested_depth(self):
        nested = library('A.TQT', squeezed())
        for i in range(12):
            nested = ark('INNER.ARK' if i else 'INNER.LBR', nested)
        name = 'OUTER.ARK'
        for i in range(13):
            result, original, decoded = inspect(nested, name)
            self.assertEqual(result['kind'], 'ARK' if i < 12 else 'LBR')
            self.assertEqual(len(result['members']), 1)
            result, original, decoded = inspect(nested, name, index=0)
            nested = original
            name = result['name']
        self.assertEqual(decoded, b'A')

    def test_invalid_member_does_not_return_archive_bytes(self):
        data = library('HELLO.TQT', squeezed())
        result, original, decoded = inspect(data, 'INNER.LBR', index=1)
        self.assertIn('Unknown archive member', result['notice'])
        self.assertIsNone(original)
        self.assertIsNone(decoded)

    def test_malformed_archive_and_bad_crc(self):
        for data, name in [(ark('A.TXT', b'A')[:-2], 'OUTER.ARK'),
                           (library('A.TXT', b'A')[:-1], 'OUTER.LBR')]:
            result, raw, decoded = inspect(data, name)
            self.assertTrue(result['notice'])
            self.assertEqual(raw, data)
            self.assertIsNone(decoded)
        data = ark('A.TXT', b'A').replace(b'A\x1a\0', b'Z\x1a\0')
        result, raw, decoded = inspect(data, 'OUTER.ARK', index=0)
        self.assertIn('CRC', result['notice'])
        self.assertEqual(raw, b'Z')
        self.assertIsNone(decoded)

    def test_unsupported_member_and_bounded_rle(self):
        packed = b'A' + b'\x90\xff' * 4200
        bomb = (b'\x1a\x03' + b'PACKED.DAT'.ljust(13, b'\0') +
                struct.pack('<IIHI', len(packed), 0, 0, 16) + packed + b'\x1a\0')
        result, original, decoded = inspect(bomb, 'BOMB.ARK', index=0)
        self.assertIn('exceeds 1 MiB', result['notice'])
        self.assertEqual(original, packed)
        self.assertIsNone(decoded)
        archive = bytearray(ark('OLD.DAT', b'A'))
        archive[1] = 7
        self.assertEqual(inspect(archive, 'OLD.ARK')[0]['kind'], 'ARK')
        result, original, decoded = inspect(archive, 'OLD.ARK', index=0)
        self.assertIn('Unsupported compression method', result['notice'])
        self.assertEqual(original, b'A')
        self.assertIsNone(decoded)

    def test_preview_limit(self):
        result, original, decoded = inspect(squeezed(), 'NOTE.TQT', truncated=True)
        self.assertIn('truncated', result['notice'])
        self.assertIsNone(decoded)
        self.assertEqual(original, squeezed())

    @unittest.skipUnless(os.environ.get('CPMIMAGE_80UN_FIXTURES'), 'external historical corpus')
    def test_historical_reference(self):
        root = Path(os.environ['CPMIMAGE_80UN_FIXTURES'])
        # Upstream records this digest from GEL UNCR24 running under a CP/M emulator.
        data = (root / 'crunch/zex-sage.dzc').read_bytes()
        _, _, decoded = inspect(data, 'ZEX-SAGE.DZC')
        self.assertEqual(hashlib.sha256(decoded).hexdigest(),
                         '11f7b57a708c4f640d17c34df19f2cb8bbb54c7acce2cd61893e0f0c6eb5ac3a')


if __name__ == '__main__':
    if len(sys.argv) == 2 and not sys.argv[1].startswith('-'):
        folder = Path(sys.argv[1])
        (folder / 'sq').write_bytes(squeezed())
        (folder / 'cr').write_bytes(crunched())
        (folder / 'lbr').write_bytes(library('HELLO.TQT', squeezed(), ('OTHER.TXT', b'Other member')))
        (folder / 'ark').write_bytes(ark('INNER.LBR', (folder / 'lbr').read_bytes())[:-2]
                                     + ark('SECOND.TXT', b'Second member'))
    else:
        unittest.main()
