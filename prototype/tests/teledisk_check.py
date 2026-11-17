# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise private TD0 import and journaled publication with local Maslin originals."""
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

import catalogue_check


def td0_crc(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0xA097 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


class TeleDiskCheck(unittest.TestCase):
    def test_sector_inventory_rejects_corrupt_and_ambiguous_records(self):
        header = b'TD' + bytes([0, 0, 21, 0, 1, 0, 0, 1])
        header += struct.pack('<H', td0_crc(header))
        data = b'A' * 128
        sector = bytes([0, 0, 1, 0, 0, td0_crc(data) & 255])
        sector += struct.pack('<H', len(data) + 1) + b'\0' + data
        end = b'\xff\0\0\0'

        def image(sectors):
            track = bytes([len(sectors), 0, 0])
            return header + track + bytes([td0_crc(track) & 255]) + b''.join(sectors) + end

        valid = image([sector])
        comment_header = header[:7] + b'\x80' + header[8:10]
        comment_header += struct.pack('<H', td0_crc(comment_header))
        comment = struct.pack('<H', 1) + bytes(6) + b'X'
        commented = comment_header + struct.pack('<H', td0_crc(comment)) + comment + valid[12:]
        for label, content, succeeds in (
            ('valid', valid, True),
            ('valid comment', commented, True),
            ('header CRC', valid[:10] + bytes([valid[10] ^ 1]) + valid[11:], False),
            ('comment CRC', commented[:12] + bytes([commented[12] ^ 1]) + commented[13:], False),
            ('track CRC', valid[:15] + bytes([valid[15] ^ 1]) + valid[16:], False),
            ('sector CRC', image([sector[:-1] + b'B']), False),
            ('missing sector data', image([sector[:4] + b'\x20' + sector[5:]]), False),
            ('duplicate ID', image([sector, sector]), False),
        ):
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / 'disk.TD0'
                output = Path(directory) / 'records'
                source.write_bytes(content)
                result = subprocess.run([os.environ['CPMIMAGE_HELPER'], 'teledisk-records',
                                         str(source), str(output)], capture_output=True)
                self.assertEqual(result.returncode == 0, succeeds, result.stdout)
                if succeeds:
                    self.assertEqual(output.read_bytes(), b'\0\0\1\x80\0' + data)
                else:
                    self.assertFalse(output.exists())

    def test_source_metadata_lifetime(self):
        first, second = (catalogue_check.CatalogueCheck() for _ in range(2))
        for tab in (first, second):
            tab.setUp()
            self.addCleanup(tab.doCleanups)
            self.addCleanup(tab.tearDown)

        def result(tab, op, **args):
            reply = tab.request(op, **args)
            self.assertTrue(reply['ok'], reply)
            return reply['result']

        original = first.image.read_bytes()
        archive = first.directory / 'Archive.TD0'
        header = b'TD' + bytes([0, 0, 21, 0, 1, 0, 0, 1])
        encoded = bytearray(header + struct.pack('<H', td0_crc(header)))
        for cylinder in range(77):
            track = bytes([26, cylinder, 0])
            encoded.extend(track + bytes([td0_crc(track) & 255]))
            for sector in range(26, 0, -1):
                start = (cylinder * 26 + sector - 1) * 128
                payload = original[start:start + 128]
                encoded.extend(bytes([cylinder, 0, sector, 0, 0, td0_crc(payload) & 255]))
                encoded.extend(b'\x81\0\0' + payload)
        encoded.extend(b'\xff\0\0\0')
        archive.write_bytes(encoded)

        token = result(first, 'open', image=str(archive), format='ibm-3740', container='td0')['session']
        summary = result(first, 'source_metadata', session=token)
        self.assertEqual(summary['source'], dict(name=archive.name, container='td0',
                                                sha256=sha256(encoded).hexdigest(), sector_order='scan'))
        self.assertEqual(len(summary['tracks']), 77)
        track = result(first, 'source_metadata', session=token, track=[0, 0])
        self.assertEqual([(row['id'], row['size']) for row in track['sectors']],
                         [(sector, 128) for sector in range(26, 0, -1)])

        other = result(second, 'open', image=str(second.image), format='ibm-3740', container='raw')['session']
        self.assertEqual(second.request('source_metadata', session=token)['error'], 'stale_session')
        self.assertEqual(result(second, 'source_metadata', session=other)['tracks'], [])

        output = first.directory / 'converted.raw'
        args = dict(session=token, destination=str(output), container='raw', sidecar=False)
        plan = result(first, 'save_plan', **args)
        result(first, 'save', plan=plan, **args)
        self.assertEqual(output.read_bytes(), original)
        self.assertEqual(archive.read_bytes(), encoded)
        self.assertEqual(result(first, 'source_metadata', session=token), summary)

        catalogue = first.directory / 'alternate.diskdefs'
        catalogue.write_text(catalogue_check.CUSTOM)
        result(first, 'catalogue', path=str(catalogue))
        options = dict(image=str(output), format='custom-3740', container='raw')
        switched = result(first, 'open', continue_session=token, **options)['session']
        self.assertEqual(first.request('source_metadata', session=token)['error'], 'stale_session')
        self.assertEqual(result(first, 'source_metadata', session=switched), summary)
        self.assertEqual(result(first, 'source_metadata', session=switched, track=[0, 0]), track)
        alias = first.directory / 'archive-alias.raw'
        os.link(archive, alias)
        forbidden = first.request('save_plan', session=switched, destination=str(alias))
        self.assertEqual(forbidden['error'], 'invalid_destination')

        reopened = result(first, 'open', **options)['session']
        metadata = result(first, 'source_metadata', session=reopened)
        self.assertEqual(metadata['source']['container'], 'raw')
        self.assertEqual(metadata['tracks'], [])

    @unittest.skipUnless(os.environ.get('CPMIMAGE_MASLIN'), 'external Maslin corpus')
    def test_import_save_reopen_and_extract(self):
        cases = (
            ('osborne/os1syss', 'osb1sssd', 'PIP.COM', '6541a35d9cf590b5e5149491893545d6632ba49a8745f0b94e8e724b8ac0d97d', '7f9e12a92e2bcfd814b5b680a2f7d5c2a2c50c9a5ef94a6891dcaa3527f08ec2'),
            ('tandy/monte-22', 'trsg', 'ASM.COM', 'ed0f007d3fcea6e9b7a5a29a6e9ade148e9ac37ef5d2abb1f2c38aaabe456bf1', 'ef403388a04f18d735984fe497f9fa5dbb48f114b52dab323e33e82073133c2c'),
            ('ampro/lb-cpm-g', 'ampro400d', 'ASM.COM', '769f8ce18d841eb349bf6e3780d79617f96d6d0ca3e38d23d41dc9e428b3efcc', 'ef403388a04f18d735984fe497f9fa5dbb48f114b52dab323e33e82073133c2c'),
            ('xerox/820sys8s', 'ibm-3740', 'STAT.COM', '97e3ea885223b745e95769d01c634579edc1a51776d78cf2844dbb5a33479ad1', '614d0b1d66466177e5b2bf585251d53d1e24eda34ada136b4ae178ab5944dc73'),
        )
        for sample, fmt, filename, raw_hash, file_hash in cases:
            with self.subTest(sample=sample):
                broker = catalogue_check.CatalogueCheck()
                broker.setUp()
                self.addCleanup(broker.doCleanups)
                self.addCleanup(broker.tearDown)
                original = Path(os.environ['CPMIMAGE_MASLIN']) / '1995/sydex' / (sample + '.td0')
                archive = broker.directory / (original.stem + '.TD0')
                shutil.copyfile(original, archive)
                source_hash = sha256(original.read_bytes()).hexdigest()
                if fmt == 'ampro400d':
                    ambiguous = broker.request('open', image=str(archive), format=fmt, container='td0')
                    self.assertEqual(ambiguous.get('error'), 'physical_mapping', ambiguous)
                    archive.with_name(archive.name + '.mapping.json').write_text(json.dumps(dict(
                        track_order=[[c, h] for c in range(40) for h in range(2)],
                        default=dict(mode=5, size=512, sectors=list(range(17, 27))))))
                inspected = broker.request('inspect', image=str(archive), explicit_format=fmt)
                self.assertTrue(inspected['ok'], inspected)
                self.assertEqual(inspected['result']['container'], 'td0')
                opened = broker.request('open', image=str(archive), format=fmt, container='td0')
                self.assertTrue(opened['ok'], opened)
                session = opened['result']['session']
                self.assertEqual(opened['result']['suggested_name'], original.stem + '.raw')
                self.assertFalse((broker.directory / (original.stem + '.raw')).exists())
                forbidden = broker.request('save_plan', session=session, destination=str(archive))
                self.assertFalse(forbidden['ok'], forbidden)
                output = broker.directory / opened['result']['suggested_name']
                args = dict(session=session, destination=str(output), container='raw')
                plan = broker.request('save_plan', **args)
                self.assertTrue(plan['ok'], plan)
                saved = broker.request('save', plan=plan['result'], confirm_sidecar=True, **args)
                self.assertTrue(saved['ok'], saved)
                self.assertEqual(sha256(output.read_bytes()).hexdigest(), raw_hash)
                reopened = broker.request('open', image=str(output), format=fmt, container='raw')
                self.assertTrue(reopened['ok'], reopened)
                destination = broker.directory / filename
                exported = broker.request('export', session=reopened['result']['session'],
                                          name='00'+filename, destination=str(destination))
                self.assertTrue(exported['ok'], exported)
                self.assertEqual(sha256(destination.read_bytes()).hexdigest(), file_hash)
                edited = broker.request('edit', session=reopened['result']['session'],
                                        confirmed=True, action='rename',
                                        name='00' + filename, target='00RENAMED.COM')
                self.assertTrue(edited['ok'], edited)
                self.assertEqual(sha256(output.read_bytes()).hexdigest(), raw_hash)
                self.assertEqual(sha256(archive.read_bytes()).hexdigest(), source_hash)
                self.assertEqual(sha256(original.read_bytes()).hexdigest(), source_hash)


if __name__ == '__main__':
    unittest.main()
