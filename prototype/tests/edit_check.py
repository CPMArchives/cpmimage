# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import unittest

from catalogue_check import CatalogueCheck
from source_ownership import Ownership


class EditCheck(unittest.TestCase):
    def test_failed_transfer_batch_leaves_both_working_directories_unchanged(self):
        source, destination = CatalogueCheck(), CatalogueCheck()
        for broker in (source, destination):
            broker.setUp()
            self.addCleanup(broker.doCleanups)
            self.addCleanup(broker.tearDown)
        destination.image.write_bytes((Path(os.environ['CPMIMAGE_ROOT']) /
                                       'fixtures/ibm3740-empty.raw').read_bytes())
        def open_image(broker):
            result = broker.request('open', image=str(broker.image), format='ibm-3740', container='raw')
            self.assertTrue(result['ok'], result)
            return result['result']['session']
        src, dst = open_image(source), open_image(destination)
        payload = source.directory / 'staged.bin'
        self.assertTrue(source.request('export', session=src, name='00BINARY.BIN',
                                       destination=str(payload))['ok'])
        too_large = source.directory / 'large.bin'
        too_large.write_bytes(bytes(512 * 1024))
        failed = destination.request('edit', session=dst, action='import', confirmed=True, items=[
            {'name': '00FIRST.BIN', 'path': str(payload)},
            {'name': '00LARGE.BIN', 'path': str(too_large)},
        ])
        self.assertFalse(failed['ok'], failed)
        absent = destination.request('export', session=dst, name='00FIRST.BIN',
                                     destination=str(destination.directory / 'unexpected.bin'))
        self.assertEqual(absent.get('error'), 'not_found', absent)
        retained = source.directory / 'retained.bin'
        self.assertTrue(source.request('export', session=src, name='00BINARY.BIN',
                                       destination=str(retained))['ok'])
        self.assertEqual(payload.read_bytes(), retained.read_bytes())

    def test_two_sessions_share_read_access_and_exclude_alias_writes(self):
        first, second = CatalogueCheck(), CatalogueCheck()
        for broker in (first, second):
            broker.setUp()
            self.addCleanup(broker.doCleanups)
            self.addCleanup(broker.tearDown)
        alias = first.directory / 'alias.raw'
        os.link(first.image, alias)
        tokens = []
        for broker, path in ((first, first.image), (second, alias)):
            opened = broker.request('open', image=str(path), format='ibm-3740', container='raw')
            self.assertTrue(opened['ok'], opened)
            tokens.append(opened['result']['session'])
        options = dict(action='delete', name='00BINARY.BIN', confirmed=True)
        self.assertTrue(first.request('edit', session=tokens[0], **options)['ok'])
        blocked = second.request('edit', session=tokens[1], **options)
        self.assertEqual(blocked.get('error'), 'ownership', blocked)
        self.assertIn('ownership', blocked['message'])
        self.assertTrue(first.request('discard', session=tokens[0])['ok'])
        self.assertTrue(second.request('edit', session=tokens[1], **options)['ok'])

    def test_competing_saves_to_shared_sidecar(self):
        first, second = CatalogueCheck(), CatalogueCheck()
        for broker in (first, second):
            broker.setUp()
            self.addCleanup(broker.doCleanups)
            self.addCleanup(broker.tearDown)
        paths = [first.directory / ('shared.' + ext) for ext in ('raw', 'img')]
        plans = []
        for broker, path in zip((first, second), paths):
            path.write_bytes(broker.image.read_bytes())
            opened = broker.request('open', image=str(path), format='ibm-3740', container='raw')
            opts = dict(session=opened['result']['session'])
            planned = broker.request('save_plan', **opts)
            self.assertTrue(planned['ok'], planned)
            plans.append(dict(plan=planned['result'], confirm_sidecar=True, **opts))
        sidecar = paths[0].with_suffix('.FDF')
        with Ownership(sidecar, Path.home() / '.cpmimage' / 'ownership'):
            blocked = second.request('save', **plans[1])
            self.assertFalse(blocked['ok'], blocked)
            self.assertIn('ownership', blocked['message'])
            self.assertFalse(sidecar.exists())
        self.assertTrue(first.request('save', **plans[0])['ok'])
        before = sidecar.read_bytes()
        stale = second.request('save', **plans[1])
        self.assertEqual(stale['error'], 'save_plan_changed')
        self.assertEqual(sidecar.read_bytes(), before)
        self.assertFalse(sidecar.with_suffix('.FDF.BAK').exists())

    def test_timestamp_survives_save_and_reopen(self):
        tools = os.environ.get('CPMIMAGE_UPSTREAM_TOOLS')
        if not tools:
            self.skipTest('Requires unmodified cpmtools')
        broker = CatalogueCheck()
        broker.setUp()
        self.addCleanup(broker.doCleanups)
        self.addCleanup(broker.tearDown)
        root = Path(os.environ['CPMIMAGE_ROOT'])
        (broker.directory / 'diskdefs').write_bytes((root / 'fixtures/diskdefs').read_bytes())
        broker.image.write_bytes(b'\xe5' * (40 * 9 * 512))
        subprocess.run([str(Path(tools) / 'mkfs.cpm'), '-f', 'pcw', '-t', str(broker.image)],
                       cwd=broker.directory, check=True)
        with broker.image.open('ab') as stream:
            stream.write(b'\xe5' * (40 * 9 * 512 - broker.image.stat().st_size))
        args = dict(image=str(broker.image), format='pcw', container='raw')
        opened = broker.request('open', **args)
        self.assertTrue(opened['ok'], opened)
        options = dict(session=opened['result']['session'])
        payload = broker.directory / 'dated.bin'
        payload.write_bytes(b'timestamp test')
        stamp = 1609459200
        os.utime(payload, (stamp, stamp))
        edited = broker.request('edit', action='import', name='00DATED.BIN', path=str(payload),
                                confirmed=True, **options)
        self.assertTrue(edited['ok'], edited)
        plan = broker.request('save_plan', **options)
        saved = broker.request('save', plan=plan['result'], confirm_sidecar=True, **options)
        self.assertTrue(saved['ok'], saved)
        reopened = broker.request('open', **args)
        self.assertTrue(reopened['ok'], reopened)
        row = next(e for e in reopened['result']['entries'] if e['name'] == '00DATED.BIN')
        self.assertEqual(row['mtime'], stamp)

    def test_batch_text_and_dsk_roundtrip(self):
        broker = CatalogueCheck()
        broker.setUp()
        self.addCleanup(broker.doCleanups)
        self.addCleanup(broker.tearDown)
        fixture = Path(os.environ['CPMIMAGE_ROOT']) / 'fixtures/ampro200-independent.dsk'
        broker.image.write_bytes(fixture.read_bytes())
        fmt = 'bettercpm-ampro-little-board'
        opened = broker.request('open', image=str(broker.image), format=fmt, container='dsk')
        self.assertTrue(opened['ok'], opened)
        options = dict(session=opened['result']['session'])
        text = broker.directory / 'text.txt'
        text.write_bytes(b'A\nB\r\nC\r')
        result = broker.request('edit', action='import', confirmed=True, **options, items=[
            dict(name='00FIRST.TXT', path=str(text), text=True),
            dict(name='00SECOND.TXT', path=str(text)),
        ])
        self.assertTrue(result['ok'], result)
        destination = broker.directory / 'copy.dsk'
        save = dict(destination=str(destination), container='dsk', **options)
        plan = broker.request('save_plan', **save)
        self.assertTrue(plan['ok'], plan)
        result = broker.request('save', plan=plan['result'], confirm_sidecar=True, **save)
        self.assertTrue(result['ok'], result)
        self.assertEqual(broker.image.read_bytes(), fixture.read_bytes())
        tools = os.environ.get('CPMIMAGE_UPSTREAM_TOOLS')
        if tools:
            (broker.directory / 'diskdefs').write_bytes(destination.with_suffix('.FDF').read_bytes())
            independent = broker.directory / 'independent.txt'
            subprocess.run([str(Path(tools) / 'cpmcp'), '-f', fmt, '-T', 'dsk', str(destination),
                            '0:first.txt', str(independent)], cwd=broker.directory, check=True)
            self.assertTrue(independent.read_bytes().startswith(b'A\r\nB\r\nC\r\n\x1a'))
        reopened = broker.request('open', image=str(destination), format=fmt, container='dsk')
        self.assertTrue(reopened['ok'], reopened)
        output = broker.directory / 'converted.txt'
        result = broker.request('export', session=reopened['result']['session'],
                                name='00FIRST.TXT', destination=str(output))
        self.assertTrue(result['ok'], result)
        self.assertTrue(output.read_bytes().startswith(b'A\r\nB\r\nC\r\n\x1a'))

    def test_edit_save_and_reopen(self):
        broker = CatalogueCheck()
        broker.setUp()
        self.addCleanup(broker.doCleanups)
        self.addCleanup(broker.tearDown)
        original = broker.image.read_bytes()
        opened = broker.request('open', image=str(broker.image), format='ibm-3740', container='raw')
        options = dict(session=opened['result']['session'])
        input_path = broker.directory / 'input.bin'
        input_path.write_bytes(bytes(range(256)) * 100)

        def edit(action, name, **fields):
            reply = broker.request('edit', action=action, name=name, confirmed=True, **options, **fields)
            self.assertTrue(reply['ok'], reply)
            return reply['result']

        edit('import', '00TEST.BIN', path=str(input_path))
        self.assertTrue(broker.request('discard', **options)['ok'])
        edit('import', '00TEST.BIN', path=str(input_path))
        edit('rename', '00TEST.BIN', target='00RENAMED.BIN')
        edit('move', '00RENAMED.BIN', target='07RENAMED.BIN')
        edit('attrs', '07RENAMED.BIN', attributes=768)
        entries = edit('delete', '00BINARY.BIN')['entries']
        self.assertNotIn('00BINARY.BIN', [entry['name'] for entry in entries])
        self.assertEqual(next(row['attr'] for row in entries if row['name'] == '07RENAMED.BIN') & 768, 768)
        self.assertEqual(broker.image.read_bytes(), original)
        plan = broker.request('save_plan', **options)
        saved = broker.request('save', plan=plan['result'], confirm_sidecar=True, **options)
        self.assertTrue(saved['ok'], saved)
        self.assertEqual(broker.image.with_name(broker.image.name + '.BAK').read_bytes(), original)
        output = broker.directory / 'check.bin'
        tools = os.environ.get('CPMIMAGE_UPSTREAM_TOOLS')
        if tools:
            (broker.directory / 'diskdefs').write_bytes(broker.image.with_suffix('.FDF').read_bytes())
            subprocess.run([str(Path(tools) / 'cpmcp'), '-f', 'ibm-3740', str(broker.image),
                            '7:renamed.bin', str(output)], cwd=broker.directory, check=True)
        else:
            result = broker.request('export', name='07RENAMED.BIN', destination=str(output), **options)
            self.assertTrue(result['ok'], result)
        self.assertEqual(output.read_bytes(), input_path.read_bytes())


if __name__ == '__main__':
    unittest.main()
