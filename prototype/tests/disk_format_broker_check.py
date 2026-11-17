# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise editor records and legacy IMD profiles with the real native helper."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from disk_format_check import IMD, RAW
from teledisk_check import td0_crc

ROOT = Path(os.environ['CPMIMAGE_ROOT']).resolve()
HELPER = Path(os.environ['CPMIMAGE_HELPER']).resolve()
TRACKS = [[0, 0], [0, 1], [1, 0], [1, 1]]
FIELDS = dict(mode=5, size=512, sectors=[1, 3, 2, 4])
MARKER = b'Physical sector ID 2'.ljust(128, b'!')


def imd_bytes(mismatch=False):
    image = bytearray(b'IMD 1.18: editor regression\r\n\x1a')
    entry = bytes([0]) + b'PROBE   BIN' + bytes([0, 0, 0, 1, 1]) + bytes(15)
    for cylinder, head in TRACKS:
        directory_track = (cylinder, head) == (0, 1)
        ids = [1, 2, 3, 5 if directory_track and mismatch else 4]
        image.extend(bytes([5, cylinder, head, 4, 2, *ids]))
        for sector in ids:
            prefix = b''
            if directory_track and sector == 1:
                prefix = entry
            elif directory_track and sector == 2:
                prefix = MARKER
            image.extend(bytes([1]) + prefix.ljust(512, b'\xe5'))
    return bytes(image)


class DiskFormatBrokerCheck(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        self.store = self.directory / 'records'
        self.defs = self.directory / 'diskdefs'
        self.defs.write_bytes(IMD.encode())
        self.image = self.directory / 'example.imd'
        self.sidecar = self.image.with_suffix('.imd.mapping.json')
        self.start_broker()

    def start_broker(self):
        broker = subprocess.Popen(
            [sys.executable, '-I', '-B', '-u', ROOT / 'prototype/broker.py',
             '--root', ROOT, '--helper', HELPER, '--definition-store', self.store],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.broker = broker
        self.addCleanup(self.close_broker, broker)

    @staticmethod
    def close_broker(broker):
        if not broker.stdin.closed:
            broker.stdin.close()
            broker.wait(timeout=20)
            broker.stdout.close()

    def request(self, op, **fields):
        self.broker.stdin.write(json.dumps(dict(v=1, id=op, op=op, **fields)) + '\n')
        self.broker.stdin.flush()
        line = self.broker.stdout.readline()
        self.assertTrue(line, 'Broker exited without reply')
        return json.loads(line)

    def success(self, op, **fields):
        reply = self.request(op, **fields)
        self.assertTrue(reply['ok'], reply)
        return reply['result']

    def mount(self):
        return self.request('open', image=str(self.image), format='example-imd-8k', container='imd')

    def write_fixture(self, profile, *, mismatch=False):
        image = imd_bytes(mismatch)
        self.image.write_bytes(image)
        self.sidecar.write_bytes(profile)
        return {self.defs: IMD.encode(), self.image: image, self.sidecar: profile}

    def assert_intact(self, inputs):
        for path, before in inputs.items():
            self.assertEqual(path.read_bytes(), before, path.name)

    def test_legacy_sidecars_open_and_export(self):
        self.success('catalogue', path=str(self.defs))
        variants = (
            dict(track_order=TRACKS, default=FIELDS, note={'legacy': True}),
            dict(track_order=TRACKS, default={},
                 tracks={f'{c}/{h}': FIELDS for c, h in TRACKS}),
        )
        for index, profile in enumerate(variants):
            with self.subTest(case=index):
                inputs = self.write_fixture((json.dumps(profile, indent=2) + '\r\n').encode())
                reply = self.mount()
                self.assertTrue(reply['ok'], reply)
                result = reply['result']
                self.assertEqual([row['name'] for row in result['entries']], ['00PROBE.BIN'])
                output = self.directory / f'export-{index}'
                self.success('export', session=result['session'], name='00PROBE.BIN',
                             destination=str(output))
                self.assertEqual(output.read_bytes(), MARKER)
                self.assert_intact(inputs)

    def test_shared_candidate_evaluation_for_short_raw(self):
        catalogue = RAW + RAW.replace('synthetic-256k', 'aaa-equivalent')
        self.defs.write_text(catalogue)
        self.success('catalogue', path=str(self.defs))
        record = self.success('definition_import', text=RAW)['record']
        raw = self.directory / 'short.raw'
        contents = b'\xe5' * (262144 - 128)
        raw.write_bytes(contents)
        inspected = self.success('inspect', image=str(raw))
        candidates = inspected['candidates']
        self.assertEqual([row['id'] for row in candidates], ['aaa-equivalent', 'synthetic-256k'])
        checked = self.success('definition_check_image', image=str(raw), record=record)
        self.assertEqual(checked['source_sha256'], inspected['source_sha256'])
        self.assertEqual(checked['evaluation'], candidates[1]['evaluation'])
        self.assertEqual(checked['evaluation']['rank'], [1, True, 128])
        self.assertEqual(checked['evaluation']['directory'], dict(status='passed', entries=0))
        self.assertEqual(checked['evaluation']['layout']['observed'], dict(bytes=len(contents)))
        self.assertFalse(raw.with_suffix('.FDF').exists())
        self.assertEqual(raw.read_bytes(), contents)
        self.assertEqual(self.success('definition_list')['records'], [])

    def test_shared_td0_candidate_evaluation_and_profile_failure(self):
        self.success('catalogue', path=str(self.defs))
        record = self.success('definition_import', text=IMD)['record']
        archive = self.directory / 'example.TD0'
        profile = archive.with_name(archive.name + '.mapping.json')
        header = b'TD' + bytes([0, 0, 21, 0, 1, 0, 0, 2])
        contents = bytearray(header + struct.pack('<H', td0_crc(header)))
        payload = b'\xe5' * 512
        for cylinder, head in TRACKS:
            track = bytes([4, cylinder, head])
            contents.extend(track + bytes([td0_crc(track) & 255]))
            for sector in (1, 3, 2, 4):
                contents.extend(bytes([cylinder, head, sector, 2, 0, td0_crc(payload) & 255]))
                contents.extend(struct.pack('<H', len(payload) + 1) + b'\0' + payload)
        contents.extend(b'\xff\0\0\0')
        archive.write_bytes(contents)
        fields = dict(image=str(archive))

        inspected = self.success('inspect', **fields)
        self.assertEqual(inspected['candidates'], [])
        self.assertEqual(len(inspected['rejected']), 1)
        self.assertEqual(inspected['rejected'][0]['code'], 'mapping_profile_required')
        self.assertIn(profile.name, inspected['rejected'][0]['detail'])
        checked = self.request('definition_check_image', record=record, **fields)
        self.assertEqual(checked['error'], 'physical_mapping')
        self.assertEqual(checked['evaluation']['layout']['code'], 'mapping_profile_required')
        self.assertEqual(checked['evaluation']['directory']['status'], 'unavailable')
        self.assertFalse(profile.exists())

        profile_bytes = json.dumps(dict(track_order=TRACKS, default=FIELDS)).encode()
        profile.write_bytes(profile_bytes)
        inspected = self.success('inspect', **fields)
        checked = self.success('definition_check_image', record=record, **fields)
        self.assertEqual(checked['container'], 'td0')
        self.assertEqual(checked['entries'], [])
        self.assertEqual(checked['evaluation'], inspected['candidates'][0]['evaluation'])
        self.assertEqual(checked['evaluation']['layout']['mapping_source'], 'sidecar')
        self.assertEqual(checked['source_sha256'], hashlib.sha256(contents).hexdigest())
        self.assertFalse(archive.with_suffix('.FDF').exists())
        self.assert_intact({archive: contents, profile: profile_bytes})

    def test_native_divisibility_rejection(self):
        invalid = IMD.replace('seclen 512', 'seclen 384').encode()
        self.defs.write_bytes(invalid)
        native = subprocess.run([HELPER, 'describe', 'example-imd-8k'], cwd=self.directory,
                                capture_output=True, check=False)
        self.assertEqual(native.returncode, 1)
        self.assertIn(b'invalid disk definition', native.stderr)
        record = self.success('definition_import', text=invalid.decode())['record']
        reply = self.request('definition_save', record=record)
        self.assertEqual(reply['error'], 'invalid_definition')
        self.assertEqual(list(self.store.glob('*.json')), [])
        self.assert_intact({self.defs: invalid})

    def test_inventory_mismatch_and_malformed_json_preserve_inputs(self):
        self.success('catalogue', path=str(self.defs))
        profile = json.dumps(dict(track_order=TRACKS, default=FIELDS)).encode()
        self.write_fixture(profile)
        self.assertTrue(self.mount()['ok'])
        record = self.success('definition_import', text=IMD)['record']
        cases = (
            (profile, True, 'Container sectors differ on track 0/1'),
            (b'{"track_order": [', False, 'Expecting value'),
        )
        for content, mismatch, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic):
                inputs = self.write_fixture(content, mismatch=mismatch)
                reply = self.mount()
                self.assertEqual(reply['error'], 'physical_mapping')
                self.assertIn(diagnostic, reply['message'])
                inspected = self.success('inspect', image=str(self.image))
                self.assertEqual(len(inspected['rejected']), 1)
                self.assertEqual(inspected['rejected'][0]['code'], 'physical_mapping')
                self.assertIn(diagnostic, inspected['rejected'][0]['detail'])
                checked = self.request('definition_check_image', image=str(self.image), record=record)
                self.assertEqual(checked['error'], 'physical_mapping')
                self.assertIn(diagnostic, checked['evaluation']['layout']['message'])
                self.assert_intact(inputs)

    def test_persistence_collision_revision_and_session_isolation(self):
        record = self.success('definition_import', text=RAW)['record']
        saved = self.success('definition_save', record=record)
        self.assertEqual(self.success('definition_export', record=record)['diskdefs'], RAW)
        created = self.success('new_image', format=record['identifier'], confirmed=True)
        other = self.success('definition_import', text=RAW)['record']
        self.assertEqual(self.request('definition_save', record=other)['error'], 'definition_store')
        raw = self.directory / 'empty.raw'
        raw.write_bytes(b'\xe5' * 262144)
        open_fields = dict(image=str(raw), format=record['identifier'], container='raw')
        checked = self.success('definition_check_image', image=str(raw), record=record)
        self.assertEqual(checked['entries'], [])
        self.assertEqual(self.success('definition_list')['records'], [record])
        record['definition']['maxdir'] = 64
        updated = self.success('definition_save', record=record, record_sha256=saved['record_sha256'])
        self.assertNotEqual(updated['definition_sha256'], saved['definition_sha256'])
        self.assertEqual(self.request('definition_save', record=record,
                                      record_sha256=saved['record_sha256'])['error'], 'definition_store')
        still_open = self.success('format_image', session=created['session'], confirmed=True)
        self.assertEqual(still_open['session'], created['session'])
        self.assertEqual(still_open['geometry']['directory_entries'], 128)
        self.assertEqual(self.request('new_image', format=record['identifier'], confirmed=True,
                                      definition_sha256=saved['definition_sha256'])['error'],
                         'definition_changed')
        self.close_broker(self.broker)
        self.start_broker()
        self.assertEqual(self.success('definition_list')['records'], [record])
        self.assertEqual(self.request('open', **open_fields,
                                      definition_sha256=saved['definition_sha256'])['error'],
                         'definition_changed')
        self.success('open', **open_fields, definition_sha256=updated['definition_sha256'])

    def test_unsupported_record_and_unresolved_import(self):
        record = self.success('definition_import', text=RAW)['record']
        self.store.mkdir()
        path = self.store / (record['record_id'] + '.json')
        future = b'{"schema_version": 2, "future": "preserve"}\n'
        path.write_bytes(future)
        self.assertEqual(self.success('definition_list')['unsupported'], [path.name])
        self.assertEqual(self.request('definition_save', record=record)['error'], 'definition_store')
        self.assert_intact({path: future})
        unresolved = self.success('definition_import', text=RAW.replace('end', '  mystery 7\nend'))['record']
        self.assertEqual(self.request('definition_export', record=unresolved)['error'], 'unresolved_import')
        self.success('definition_export', record=unresolved, resolved_import=True)

    def test_explicit_mapping_conversion(self):
        record = self.success('definition_import', text=IMD)['record']
        legacy = dict(track_order=TRACKS, default={}, note='preserve until confirmed',
                      tracks={f'{c}/{h}': FIELDS for c, h in TRACKS})
        inputs = self.write_fixture(json.dumps(legacy).encode())
        options = dict(record=record, image=str(self.image))
        preview = self.success('mapping_preview', **options)
        self.assertEqual(preview['previous_profile'], legacy)
        self.assertEqual(preview['profile'], dict(track_order=TRACKS, default=FIELDS))
        digest = hashlib.sha256(inputs[self.sidecar]).hexdigest()
        self.assertEqual(preview['mapping_sha256'], digest)
        self.assertEqual(self.request('mapping_write', **options, profile=preview['profile'],
                                      mapping_sha256=digest)['error'], 'confirmation_required')
        self.assertEqual(self.request('mapping_write', **options, profile=legacy,
                                      mapping_sha256=digest, confirmed=True)['error'], 'invalid_mapping')
        self.assert_intact(inputs)
        written = self.success('mapping_write', **options, profile=preview['profile'],
                               mapping_sha256=digest, confirmed=True)
        self.assertEqual(written['mapping_sha256'], hashlib.sha256(self.sidecar.read_bytes()).hexdigest())
        self.assertEqual(json.loads(self.sidecar.read_bytes()), preview['profile'])
        self.assertEqual(self.request('mapping_write', **options, profile=preview['profile'],
                                      mapping_sha256=digest, confirmed=True)['error'], 'source_changed')
        self.assert_intact({self.image: inputs[self.image], self.defs: inputs[self.defs]})


if __name__ == '__main__':
    unittest.main()
