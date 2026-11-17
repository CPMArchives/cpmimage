# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise diagnostic directory reads independently of the normal strict mount."""
import hashlib
import unittest

import catalogue_check


class FilesystemCheck(unittest.TestCase):
    def setUp(self):
        self.tab = catalogue_check.CatalogueCheck()
        self.tab.setUp()
        self.addCleanup(self.tab.doCleanups)
        self.addCleanup(self.tab.tearDown)
        self.blank = (catalogue_check.ROOT / 'fixtures/ibm3740-empty.raw').read_bytes()

    def result(self, operation, **fields):
        reply = self.tab.request(operation, **fields)
        self.assertTrue(reply['ok'], reply)
        return reply['result']

    @staticmethod
    def entry(name=b'FILE    BIN', *, extent_low=0, extent_high=0, records=1, last=0, blocks=()):
        return (b'\0' + name + bytes((extent_low, last, extent_high, records))
                + bytes(blocks).ljust(16, b'\0'))

    def write(self, path, *entries, length=None):
        # IBM 3740's first directory sector follows the two boot tracks.
        offset = 2 * 26 * 128
        data = bytearray(self.blank)
        data[offset:offset + 32 * len(entries)] = b''.join(entries)
        if length is not None:
            data = data[:length]
        path.write_bytes(data)
        return bytes(data)

    def test_working_revision_and_external_damage(self):
        self.tab.image.write_bytes(self.blank)
        opened = self.result('open', image=str(self.tab.image), format='ibm-3740', container='raw')
        session = opened['session']
        original = self.result('filesystem_check', session=session)
        self.assertEqual((original['status'], original['mode'], original['dirty']), ('clean', 'working', False))
        self.assertEqual(original['findings'], [])
        payload = self.tab.directory / 'payload.bin'
        payload.write_bytes(b'A' * 20000)
        self.result('edit', session=session, action='import', confirmed=True, name='00PAYLOAD.BIN', path=str(payload))
        stale = self.tab.request('filesystem_check', session=session, revision=original['revision'])
        self.assertEqual(stale['error'], 'stale_revision')
        working = self.result('filesystem_check', session=session)
        self.assertEqual(working['status'], 'clean')
        self.assertTrue(working['dirty'])

        damaged = self.tab.directory / 'damaged.raw'
        data = self.write(damaged, self.entry(extent_low=0x20, extent_high=0x40, records=129, last=130, blocks=(2, 1)),
                          self.entry(blocks=(2, 250)), b'\x7f' + bytes(31))
        fields = dict(image=str(damaged), format='ibm-3740')
        report = self.result('filesystem_check', **fields)
        self.assertEqual(report['status'], 'findings')
        self.assertEqual(report['phase'], 'complete')
        self.assertEqual(report['mode'], 'external')
        self.assertEqual(report['source_sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(report, self.result('filesystem_check', **fields))
        findings = {finding['code']: finding for finding in report['findings']}
        self.assertEqual(set(findings), {'invalid_extent_low', 'invalid_extent_high', 'invalid_record_count',
                         'invalid_last_record_count', 'reserved_block', 'shared_block', 'block_out_of_range',
                         'invalid_status', 'duplicate_extent'})
        self.assertEqual((findings['shared_block']['slot'], findings['shared_block']['related_slot'],
                          findings['shared_block']['block']), (1, 0, 2))
        self.assertEqual(report['finding_count'], len(report['findings']))
        self.assertEqual(damaged.read_bytes(), data)
        self.assertFalse(damaged.with_suffix('.FDF').exists())
        self.assertEqual(self.tab.image.read_bytes(), self.blank)
        self.assertEqual(self.result('filesystem_check', session=session, revision=working['revision']), working)
        self.assertEqual(self.tab.request('open', container='raw', **fields)['error'], 'image_incompatible')

    def test_missing_sectors_and_incomplete_reports(self):
        missing = self.tab.directory / 'missing.raw'
        original = self.write(missing, self.entry(blocks=(242,)), length=13001)
        fields = dict(image=str(missing), format='ibm-3740')
        report = self.result('filesystem_check', **fields)
        self.assertEqual(report['status'], 'findings')
        self.assertEqual([(row['code'], row['block']) for row in report['findings']],
                         [('missing_allocated_sectors', 242)])
        missing.write_bytes(original[:6657])
        report = self.result('filesystem_check', **fields)
        self.assertEqual((report['status'], report['phase'], report['findings']), ('incomplete', 'directory', []))
        self.assertEqual(missing.read_bytes(), original[:6657])

        imd = self.tab.directory / 'malformed.imd'
        content = b'IMD 1.18: malformed\x1a\x05'
        imd.write_bytes(content)
        report = self.result('filesystem_check', image=str(imd), format='ibm-3740')
        self.assertEqual((report['status'], report['phase'], report['findings']), ('incomplete', 'container', []))
        self.assertEqual(imd.read_bytes(), content)
        self.assertEqual(self.tab.request('filesystem_check', image=str(imd))['error'], 'unknown_format')

        dsk = self.tab.directory / 'standard.dsk'
        content = (catalogue_check.ROOT / 'fixtures/ampro200-independent.dsk').read_bytes()
        dsk.write_bytes(content)
        report = self.result('filesystem_check', image=str(dsk), format='bettercpm-ampro-little-board')
        self.assertEqual((report['status'], report['container']), ('clean', 'dsk'))
        self.assertEqual(dsk.read_bytes(), content)

    def test_findings_are_bounded_and_counted(self):
        catalogue = self.tab.directory / 'linear.diskdefs'
        catalogue.write_text(catalogue_check.CUSTOM.replace('skew 6', 'skew 1'))
        self.result('catalogue', path=str(catalogue))
        path = self.tab.directory / 'invalid-names.raw'
        data = self.write(path, *([self.entry(name=bytes(11))] * 64))
        report = self.result('filesystem_check', image=str(path), format='custom-3740')
        self.assertEqual(report['status'], 'findings')
        self.assertEqual(len(report['findings']), 256)
        self.assertEqual(report['finding_count'], 64 * 11 + 63)
        self.assertEqual(report['omitted'], report['finding_count'] - len(report['findings']))
        self.assertEqual(path.read_bytes(), data)

    def test_verified_extent_repair_discard_and_save(self):
        # Directory slot 4 starts logical sector 1, stored in physical slot 6
        # by the IBM 3740 skew. Keep every other byte as a preservation check.
        address = (2 * 26 + 6) * 128
        damaged = bytearray(self.blank)
        damaged[address:address + 32] = self.entry(extent_low=32, extent_high=64, blocks=(2,))
        self.tab.image.write_bytes(damaged)
        opened = self.result('open', image=str(self.tab.image), format='ibm-3740', container='raw')
        session = opened['session']
        check = self.result('filesystem_check', session=session)
        self.assertEqual({row['code'] for row in check['findings']},
                         {'invalid_extent_low', 'invalid_extent_high'})
        args = dict(session=session, action='repair_extent_flags', revision=check['revision'])
        self.assertEqual(self.tab.request('edit', **args)['error'], 'confirmation_required')
        self.assertTrue(self.result('edit', confirmed=True, **args)['dirty'])
        self.assertEqual(self.result('filesystem_check', session=session)['status'], 'clean')
        self.assertEqual(self.tab.image.read_bytes(), damaged)
        self.assertEqual(self.tab.request('edit', confirmed=True, **args)['error'], 'stale_revision')
        self.assertFalse(self.result('discard', session=session)['dirty'])
        check = self.result('filesystem_check', session=session)
        self.assertEqual(check['finding_count'], 2)
        self.result('edit', session=session, action='repair_extent_flags',
                    confirmed=True, revision=check['revision'])
        options = dict(session=session, destination=str(self.tab.image))
        plan = self.result('save_plan', **options)
        self.result('save', plan=plan, confirm_sidecar=True, **options)
        expected = bytearray(damaged)
        expected[address + 12] = 0
        expected[address + 14] = 0
        self.assertEqual(self.tab.image.read_bytes(), expected)
        self.assertEqual(self.tab.image.with_name(self.tab.image.name + '.BAK').read_bytes(), damaged)
        self.assertEqual(self.result('filesystem_check', session=session)['status'], 'clean')

    def test_mixed_findings_cannot_be_applied(self):
        damaged = self.write(self.tab.image, self.entry(extent_low=32, last=129, blocks=(2,)))
        session = self.result('open', image=str(self.tab.image), format='ibm-3740', container='raw')['session']
        check = self.result('filesystem_check', session=session)
        rejected = self.tab.request('edit', session=session, action='repair_extent_flags',
                                    revision=check['revision'], confirmed=True)
        self.assertEqual(rejected['error'], 'unsupported_repair')
        self.assertEqual(self.result('filesystem_check', session=session), check)
        self.assertEqual(self.tab.image.read_bytes(), damaged)

    def test_cpm_plus_metadata_is_recognized(self):
        created = self.result('new_image', format='pcw', confirmed=True, label='PROOF', timestamps=True)
        report = self.result('filesystem_check', session=created['session'])
        self.assertEqual(report['status'], 'clean')
        self.assertEqual(report['findings'], [])


if __name__ == '__main__':
    unittest.main()
