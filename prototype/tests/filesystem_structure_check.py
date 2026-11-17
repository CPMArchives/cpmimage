# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise filesystem inspection through the broker and pinned native helper."""
import unittest

import catalogue_check


class FilesystemStructureCheck(unittest.TestCase):
    def tab(self):
        tab = catalogue_check.CatalogueCheck()
        tab.setUp()
        self.addCleanup(tab.doCleanups)
        self.addCleanup(tab.tearDown)
        return tab

    def result(self, tab, operation, **fields):
        reply = tab.request(operation, **fields)
        self.assertTrue(reply['ok'], reply)
        return reply['result']

    def test_working_extents_blocks_and_revisions(self):
        tab = self.tab()
        other = self.tab()
        tab.image.write_bytes((catalogue_check.ROOT / 'fixtures/ibm3740-empty.raw').read_bytes())
        original = tab.image.read_bytes()
        fields = dict(image=str(tab.image), format='ibm-3740', container='raw')
        token = self.result(tab, 'open', **fields)['session']
        initial = self.result(tab, 'filesystem_structure', session=token, view='directory', limit=2)
        self.assertEqual(initial['total'], 64)
        self.assertEqual(initial['pointer_bytes'], 1)
        self.assertEqual(initial['rows'], [
            dict(slot=0, status=229, kind='unused'), dict(slot=1, status=229, kind='unused')])
        self.assertEqual(initial['next_start'], 2)
        following = self.result(tab, 'filesystem_structure', session=token, view='directory',
                                start=initial['next_start'], limit=2, revision=initial['revision'])
        self.assertEqual([row['slot'] for row in following['rows']], [2, 3])
        self.assertEqual(following['revision'], initial['revision'])
        self.assertFalse(initial['dirty'])
        self.result(other, 'open', image=str(other.image), format='ibm-3740', container='raw')
        self.assertEqual(other.request('filesystem_structure', session=token, view='directory')['error'],
                         'stale_session')
        for options in (dict(view='blocks', limit=129), dict(view='extents', start=True), dict(view='references')):
            self.assertEqual(tab.request('filesystem_structure', session=token, **options)['error'], 'invalid_request')

        payload = tab.directory / 'payload.bin'
        payload.write_bytes(b'P' * 20000)
        edited = self.result(tab, 'edit', session=token, action='import', confirmed=True,
                             items=[dict(name=name, path=str(payload)) for name in ('00SAME.BIN', '01SAME.BIN')])
        stale = tab.request('filesystem_structure', session=token, view='directory', revision=initial['revision'])
        self.assertEqual(stale['error'], 'stale_revision')
        extents = self.result(tab, 'filesystem_structure', session=token, view='extents')
        self.assertEqual(extents['total'], 4)
        slots = self.result(tab, 'filesystem_structure', session=token, view='directory')
        self.assertEqual([row['extent_index'] for row in slots['rows'] if row['kind'] == 'file'],
                         [0, 1, 2, 3])
        self.assertTrue(extents['dirty'])
        self.assertEqual([(row['user'], row['name'], row['extension'], row['extent'])
                          for row in extents['rows']],
                         [(0, 'SAME', 'BIN', 0), (0, 'SAME', 'BIN', 1),
                          (1, 'SAME', 'BIN', 0), (1, 'SAME', 'BIN', 1)])
        self.assertEqual(extents['rows'][0]['record_count'], 128)
        self.assertEqual(extents['rows'][1]['record_count'], 29)
        self.assertIn(None, extents['rows'][1]['pointers'])
        revision = extents['revision']
        allocation = {}
        start = 0
        while start is not None:
            page = self.result(tab, 'filesystem_structure', session=token, view='blocks',
                               revision=revision, start=start)
            allocation.update({row['block']: row['state'] for row in page['rows']})
            start = page['next_start']
        self.assertEqual(len(allocation), extents['block_count'])
        self.assertEqual(allocation[0], 'directory')
        self.assertEqual(sum(state == 'free' for state in allocation.values()) * extents['block_size'],
                         edited['free_bytes'])
        first = extents['rows'][0]
        block = first['pointers'][0]
        self.assertEqual(allocation[block], 'allocated')
        refs = self.result(tab, 'filesystem_structure', session=token, view='references',
                           block=block, revision=revision, limit=1)
        self.assertEqual(refs['rows'], [dict(slot=first['slot'], pointer=0)])
        self.assertIsNone(refs['next_start'])
        self.assertEqual(self.result(tab, 'filesystem_structure', session=token, view='references',
                                     block=0, revision=revision)['rows'], [])
        self.assertEqual(tab.request('filesystem_structure', session=token, view='references',
                                     block=extents['block_count'])['error'], 'unknown_block')

        self.assertEqual(tab.image.read_bytes(), original)
        self.assertFalse(tab.image.with_suffix('.FDF').exists())
        self.result(tab, 'discard', session=token)
        self.assertEqual(tab.request('filesystem_structure', session=token, view='blocks',
                                     revision=revision)['error'], 'stale_revision')
        discarded = self.result(tab, 'filesystem_structure', session=token, view='extents')
        self.assertFalse(discarded['dirty'])
        self.assertEqual(discarded['rows'], [])
        reopened = self.result(tab, 'open', continue_session=token, **fields)['session']
        self.assertEqual(tab.request('filesystem_structure', session=token, view='directory')['error'],
                         'stale_session')
        self.assertEqual(tab.request('filesystem_structure', session=reopened, view='directory',
                                     revision=discarded['revision'])['error'], 'stale_revision')

    def test_cpm_plus_and_wide_block_pointers(self):
        tab = self.tab()
        catalogue = tab.directory / 'wide.diskdefs'
        catalogue.write_text('diskdef wide\n seclen 128\n tracks 200\n sectrk 64\n'
                             ' blocksize 4096\n maxdir 128\n boottrk 0\n os 3\nend\n')
        self.result(tab, 'catalogue', path=str(catalogue))
        opened = self.result(tab, 'new_image', format='wide', confirmed=True,
                             label='PROOF', timestamps=True)
        token = opened['session']
        payload = tab.directory / 'large.bin'
        payload.write_bytes(b'Z' * (260 * 4096))
        self.result(tab, 'edit', session=token, action='import', confirmed=True,
                    name='00WIDE.BIN', path=str(payload))
        entries = self.result(tab, 'filesystem_structure', session=token, view='directory')
        self.assertEqual(entries['pointer_bytes'], 2)
        self.assertEqual(entries['extent_factor'], 2)
        self.assertEqual(entries['rows'][0], dict(slot=0, status=32, kind='label'))
        self.assertEqual(entries['rows'][3], dict(slot=3, status=33, kind='timestamp'))
        high = next(row for row in entries['rows'] if row['kind'] == 'file'
                    and any(block and block > 255 for block in row['pointers']))
        self.assertEqual(len(high['pointers']), 8)
        self.assertEqual(high['physical_extent'], high['extent'] // entries['extent_factor'])
        pointer, block = next((index, block) for index, block in enumerate(high['pointers'])
                              if block and block > 255)
        refs = self.result(tab, 'filesystem_structure', session=token, view='references', block=block)
        self.assertEqual(refs['rows'], [dict(slot=high['slot'], pointer=pointer)])


if __name__ == '__main__':
    unittest.main()
