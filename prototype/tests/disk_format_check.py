# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from catalogue import CatalogueError, import_definition
from disk_format import DiskFormatError, render_definition, validate_mapping_sidecar


RAW = """diskdef synthetic-256k
  seclen 128
  tracks 64
  sectrk 32
  blocksize 1024
  maxdir 128
  boottrk 0
end
"""

IMD = """diskdef example-imd-8k
  seclen 512
  tracks 4
  sectrk 4
  blocksize 1024
  maxdir 32
  bootsec 4
  os 2.2
end
"""


class DiskFormatCheck(unittest.TestCase):
    def test_examples_round_trip(self):
        for example in (RAW, IMD):
            with self.subTest(example=example.splitlines()[0]):
                imported = import_definition(example)
                self.assertEqual(imported.diagnostics, ())
                rendered = render_definition(imported.record)
                self.assertEqual(rendered.text.encode('utf-8'), example.encode('utf-8'))
                self.assertEqual(rendered.revision, hashlib.sha256(example.encode('utf-8')).hexdigest())
                self.assertEqual(render_definition(import_definition(example).record).revision,
                                 rendered.revision)

    def test_definition_structure(self):
        record = import_definition(RAW).record
        for suffix in ('\n', '\r\n'):
            with self.subTest(suffix=suffix), self.assertRaises(DiskFormatError):
                render_definition(dict(record, identifier=record['identifier'] + suffix))
        with self.assertRaisesRegex(DiskFormatError, 'record_id'):
            render_definition(dict(record, record_id='not-a-uuid'))
        with self.assertRaises(DiskFormatError):
            render_definition(dict(record, schema_version=2))
        record['definition']['seclen'] = 128.0
        self.assertEqual(render_definition(record).text, RAW)

    def test_strict_sidecar(self):
        profile = {'track_order': [[0, 0], [0, 1], [1, 0], [1, 1]],
                   'default': {'mode': 5, 'size': 512, 'sectors': [1, 3, 2, 4]}}
        validate_mapping_sidecar(profile)
        validate_mapping_sidecar(dict(profile, tracks={'0/1': {'mode': 2}}))
        for suffix in ('\n', '\r\n'):
            with self.subTest(suffix=suffix), self.assertRaises(DiskFormatError):
                validate_mapping_sidecar(dict(profile, tracks={'0/1' + suffix: {}}))
        for invalid in (dict(profile, note='legacy'),
                        dict(profile, default={}, tracks={'0/0': profile['default']}),
                        dict(profile, track_order=[[0, 0], [0, 0]])):
            with self.subTest(invalid=invalid), self.assertRaises(DiskFormatError):
                validate_mapping_sidecar(invalid)

    def test_import_preserves_source_and_precedence(self):
        extra = ('  bootsec 0\n  skew 2\n  skewtab 0, 1, 2, 3\n'
                 '  OS 2.2\n  os 3\n  tracks 04\n  mystery 7\n'
                 '  dirblks 0\n  logicalextents 0\n  offset 2Kbytes\n')
        text = IMD.replace('end\n', extra + 'end\n').replace('\n', '\r\n')
        imported = import_definition(text)
        fields = imported.record['definition']
        self.assertEqual(imported.record['import_source']['diskdefs_text'], text)
        self.assertEqual(fields['boot'], {'unit': 'sectors', 'count': 0})
        self.assertEqual(fields['skew'], {'mode': 'table', 'slots': [0, 1, 2, 3]})
        self.assertEqual(fields['tracks'], 4)
        self.assertIsNone(fields['dirblks'])
        self.assertIsNone(fields['logicalextents'])
        self.assertEqual(fields['offset'], {'value': 2, 'unit': 'kib'})
        self.assertEqual({item.code for item in imported.diagnostics},
                         {'duplicate_directive', 'multiple_os', 'redundant_directive',
                          'unknown_directive', 'noncanonical_offset'})

    def test_import_errors_and_legacy_numeric_spellings(self):
        imported = import_definition(RAW.replace('128\n  tracks', '0x80\n  tracks')
                                     .replace('tracks 64', 'tracks 0100'))
        self.assertEqual(imported.diagnostics, ())
        self.assertEqual(render_definition(imported.record).text, RAW)
        imported = import_definition(RAW.replace('  seclen 128\n', '  offset 2T\n  seclen 09\n'))
        self.assertEqual([item.code for item in imported.diagnostics],
                         ['invalid_value', 'invalid_value', 'missing_directive'])
        for invalid in (RAW + IMD, RAW + '\0', '#' * 255 + '\n' + RAW):
            with self.subTest(invalid=invalid), self.assertRaises(CatalogueError):
                import_definition(invalid)

    def test_render_offset_and_line_limits(self):
        imported = import_definition(IMD.replace('end\n', '  offset 2T\nend\n'))
        fields = imported.record['definition']
        self.assertEqual(fields['offset'], {'value': 2, 'unit': 'tracks'})
        self.assertIn('  offset 4096\n', render_definition(imported.record).text)
        fields['sectrk'] = 8
        self.assertIn('  offset 8192\n', render_definition(imported.record).text)
        fields['offset']['value'] = 2147483647
        with self.assertRaisesRegex(DiskFormatError, 'effective byte offset'):
            render_definition(imported.record)
        fields['offset']['value'] = 0
        fields['libdsk_format'] = 'é' * 130
        with self.assertRaisesRegex(DiskFormatError, 'line limit'):
            render_definition(imported.record)

    @unittest.skipUnless(os.environ.get('CPMIMAGE_HELPER'), 'requires CPMIMAGE_HELPER')
    def test_native_description_and_divisibility(self):
        helper = os.environ['CPMIMAGE_HELPER']
        for text, boot, directory in ((RAW, 0, 4), (IMD, 4, 1)):
            info = render_definition(import_definition(text).record).describe(helper)
            self.assertEqual((info['boot_sectors'], info['directory_blocks'], info['extent_factor']),
                             (boot, directory, 1))
        invalid = render_definition(import_definition(IMD.replace('seclen 512', 'seclen 384')).record)
        with self.assertRaisesRegex(DiskFormatError, 'invalid disk definition'):
            invalid.describe(helper)


if __name__ == '__main__':
    unittest.main()
