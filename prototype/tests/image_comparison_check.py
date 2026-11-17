# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise comparison with separate brokers and real CP/M working images."""
import hashlib
import unittest

import catalogue_check


class ImageComparisonCheck(unittest.TestCase):
    def tab(self):
        broker = catalogue_check.CatalogueCheck()
        broker.setUp()
        self.addCleanup(broker.doCleanups)
        self.addCleanup(broker.tearDown)
        broker.image.write_bytes((catalogue_check.ROOT / 'fixtures/ibm3740-empty.raw').read_bytes())
        return broker

    def result(self, broker, op, **fields):
        reply = broker.request(op, **fields)
        self.assertTrue(reply['ok'], reply)
        return reply['result']

    def populate(self, broker, files):
        token = self.result(broker, 'open', image=str(broker.image),
                            format='ibm-3740', container='raw')['session']
        items = []
        for index, (name, content) in enumerate(files):
            payload = broker.directory / f'payload-{index}'
            payload.write_bytes(content)
            items.append(dict(name=name, path=str(payload)))
        self.result(broker, 'edit', session=token, action='import', items=items, confirmed=True)
        return token

    def manifest(self, broker, token):
        first = self.result(broker, 'comparison_manifest', session=token, limit=1)
        revision = first['revision']
        rest = self.result(broker, 'comparison_manifest', session=token, revision=revision,
                           start=first['next_start'])
        self.assertIsNone(rest['next_start'])
        files = {row['name']: row for row in first['rows'] + rest['rows']}
        self.assertEqual(len(files), first['total'])
        self.assertEqual(list(files), sorted(files))
        self.assertTrue(first['dirty'])
        return first, files

    def test_compare_user_areas_full_contents_attributes_and_structure(self):
        left, right = self.tab(), self.tab()
        source = left.image.read_bytes()
        left_token = self.populate(left, [
            ('00SAME.BIN', b'A' * 20000), ('00CHANGED.BIN', b'B' * 128),
            ('00ATTR.BIN', b'C' * 128), ('01MOVED.BIN', b'D' * 128),
            ('00SIZE.BIN', b'E' * 128), ('00BOTH.BIN', b'F' * 128),
        ])
        right_token = self.populate(right, [
            ('00CHANGED.BIN', b'X' * 128), ('00SAME.BIN', b'A' * 20000),
            ('00ATTR.BIN', b'C' * 128), ('02MOVED.BIN', b'D' * 128),
            ('00SIZE.BIN', b'E' * 256), ('00BOTH.BIN', b'G' * 128),
        ])
        for name in ('00ATTR.BIN', '00BOTH.BIN'):
            self.result(right, 'edit', session=right_token, action='attrs', name=name,
                        attributes=256, confirmed=True)
        left_manifest, left_files = self.manifest(left, left_token)
        right_manifest, right_files = self.manifest(right, right_token)
        self.assertNotEqual(left_manifest['revision'], right_manifest['revision'])
        self.assertEqual(right.request('comparison_manifest', session=left_token)['error'], 'stale_session')
        self.assertEqual(left.request('comparison_manifest', session=left_token,
                                      revision=right_manifest['revision'])['error'], 'stale_revision')
        comparable = sorted(name for name in left_files.keys() & right_files.keys()
                            if left_files[name]['size'] == right_files[name]['size'])
        directories = []
        for broker, token, manifest, files in (
                (left, left_token, left_manifest, left_files), (right, right_token, right_manifest, right_files)):
            context = dict(session=token, revision=manifest['revision'])
            hashes = self.result(broker, 'comparison_fingerprints', names=comparable, **context)['rows']
            for row in hashes:
                files[row['name']].update(row)
            self.assertEqual(files['00SAME.BIN']['size'], 20000)
            self.assertEqual(files['00SAME.BIN']['sha256'],
                             hashlib.sha256(b'A' * 20000).hexdigest())
            directory = self.result(broker, 'filesystem_structure', view='directory', **context)
            directories.append(directory['rows'])
            for row in directory['rows']:
                if row['kind'] == 'file':
                    name = f"{row['user']:02d}{row['name']}.{row['extension']}"
                    files[name].setdefault('extents', []).append(row)
        definitions = dict(left_definition=left_manifest['definition_sha256'],
                           right_definition=right_manifest['definition_sha256'])
        pairs = [dict(left=left_files.get(name), right=right_files.get(name))
                 for name in sorted(left_files.keys() | right_files.keys(), reverse=True)]
        report = self.result(left, 'comparison_classify', view='files', pairs=pairs, **definitions)
        self.assertTrue(report['same_definition'])
        files = {row['name']: row for row in report['rows']}
        self.assertEqual(list(files), sorted(files))
        self.assertEqual((files['01MOVED.BIN']['status'], files['01MOVED.BIN']['user']), ('removed', 1))
        self.assertEqual((files['02MOVED.BIN']['status'], files['02MOVED.BIN']['user']), ('added', 2))
        self.assertEqual(files['00SAME.BIN']['status'], 'identical')
        self.assertFalse(files['00SAME.BIN']['structure']['extents_changed'])
        self.assertTrue(files['00SAME.BIN']['structure']['slots_changed'])
        self.assertTrue(files['00SAME.BIN']['structure']['allocation_changed'])
        self.assertTrue(files['00CHANGED.BIN']['content_changed'])
        self.assertFalse(files['00CHANGED.BIN']['attributes_changed'])
        self.assertTrue(files['00ATTR.BIN']['attributes_changed'])
        self.assertFalse(files['00ATTR.BIN']['content_changed'])
        self.assertTrue(files['00SIZE.BIN']['content_changed'])
        self.assertTrue(files['00BOTH.BIN']['attributes_changed'])
        self.assertTrue(files['00BOTH.BIN']['content_changed'])
        slot_report = self.result(left, 'comparison_classify', view='directory',
                                  pairs=[dict(left=a, right=b) for a, b in zip(*directories)], **definitions)
        self.assertEqual(slot_report['rows'][0]['status'], 'changed')
        self.assertEqual(slot_report['rows'][-1]['status'], 'identical')
        for broker, token, manifest in ((left, left_token, left_manifest), (right, right_token, right_manifest)):
            current = self.result(broker, 'comparison_manifest', session=token, revision=manifest['revision'])
            self.assertTrue(current['dirty'])
            self.assertEqual(broker.image.read_bytes(), source)
            self.assertFalse(broker.image.with_suffix('.FDF').exists())
        self.result(left, 'discard', session=left_token)
        stale = left.request('comparison_fingerprints', session=left_token, revision=left_manifest['revision'],
                             names=['00SAME.BIN'])
        self.assertEqual(stale['error'], 'stale_revision')

    def test_unreadable_data_and_different_definitions(self):
        broker = self.tab()
        extent = dict(slot=1, extent=0, physical_extent=0, record_count=1, pointers=[4, None])
        left = dict(name='00A.BIN', size=128, attr=0, extents=[extent], sha256='c' * 64)
        right = dict(left, attr=256, extents=[dict(extent, pointers=[2, None])], sha256=None)
        pairs = [dict(left=left, right=right)]
        definitions = dict(left_definition='a' * 64, right_definition='b' * 64)
        report = self.result(broker, 'comparison_classify', view='files', pairs=pairs, **definitions)
        self.assertFalse(report['same_definition'])
        row = report['rows'][0]
        self.assertEqual(row['status'], 'unavailable')
        self.assertIsNone(row['content_changed'])
        self.assertTrue(row['attributes_changed'])
        self.assertIsNone(row['structure']['allocation_changed'])
        self.assertFalse(row['structure']['extents_changed'])
        left_slot = dict(extent, kind='file', status=0, name='A', extension='BIN', user=0, attributes=0)
        right_slot = dict(left_slot, pointers=[2, None])
        dir_report = self.result(broker, 'comparison_classify', view='directory',
                                 pairs=[dict(left=left_slot, right=right_slot)], **definitions)
        self.assertEqual(dir_report['rows'][0]['status'], 'unavailable')
        self.assertIsNone(dir_report['rows'][0]['allocation_changed'])
        right['name'] = '01A.BIN'
        rejected = broker.request('comparison_classify', view='files', pairs=pairs, **definitions)
        self.assertEqual(rejected['error'], 'invalid_request')


if __name__ == '__main__':
    unittest.main()
