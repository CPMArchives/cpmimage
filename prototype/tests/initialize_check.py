# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from catalogue_check import CatalogueCheck


class InitializeCheck(CatalogueCheck):
    def test_plus_and_structured_creation(self):
        import json
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from container_mapping import inspect_imd
        boot = self.directory / 'boot'
        boot.write_bytes(b'BOOT')
        for container, fmt in [('raw', 'pcw'), ('imd', 'ibm-3740'), ('jv3', 'ibm-3740'),
                               ('dmk', 'ibm-3740'), ('dsk', 'bettercpm-ampro-little-board')]:
            with self.subTest(container=container):
                plus = fmt == 'pcw'
                created = self.request('new_image', format=fmt, confirmed=True,
                                       label='PROOF' if plus else '', timestamps=plus,
                                       **({'boot_path': str(boot), 'pad': True} if plus else {}))
                self.assertTrue(created['ok'], created)
                target = self.directory / ('new.' + container)
                if container in ('imd', 'jv3', 'dmk'):
                    profile = dict(track_order=[[i, 0] for i in range(77)],
                                   default=dict(mode=5, size=128, sectors=list(range(1, 27))))
                    target.with_name(target.name + '.mapping.json').write_text(json.dumps(profile))
                options = dict(session=created['result']['session'], destination=str(target), container=container)
                plan = self.request('save_plan', **options)
                self.assertTrue(plan['ok'], plan)
                saved = self.request('save', plan=plan['result'], confirm_sidecar=True, **options)
                self.assertTrue(saved['ok'], saved)
                opened = self.request('open', image=str(target), format=fmt, container=container)
                self.assertTrue(opened['ok'], opened)
                self.assertEqual(opened['result']['entries'], [])
                if plus:
                    content = target.read_bytes()
                    self.assertEqual(content[:4608], b'BOOT' + b'\xe5' * 4604)
                    self.assertEqual(content[4608:4621], b'\x20PROOF      \x11')
                    self.assertEqual(content[4704], 0x21)
                if container == 'imd':
                    self.assertEqual(len(inspect_imd(target.read_bytes())[0]), 77)
                token = opened['result']['session']
                formatted = self.request('format_image', session=token, confirmed=True)
                self.assertTrue(formatted['ok'], formatted)
                plan = self.request('save_plan', session=token)
                saved = self.request('save', session=token, plan=plan['result'], confirm_sidecar=True)
                self.assertTrue(saved['ok'], saved)

    def test_failed_initialization_and_destination_conflict(self):
        opened = self.request('open', image=str(self.image), format='ibm-3740', container='raw')
        self.assertTrue(opened['ok'], opened)
        token = opened['result']['session']
        original = self.image.read_bytes()
        refused = self.request('format_image', session=token)
        self.assertEqual(refused['error'], 'confirmation_required')
        refused = self.request('format_image', session=token, confirmed=True, label='BAD')
        self.assertEqual(refused['error'], 'unsupported_backend')
        self.assertEqual(self.image.read_bytes(), original)
        created = self.request('new_image', format='ibm-3740', confirmed=True)
        self.assertTrue(created['ok'], created)
        options = dict(session=created['result']['session'], destination=str(self.image))
        plan = self.request('save_plan', **options)
        self.assertTrue(plan['ok'], plan)
        refused = self.request('save', plan=plan['result'], confirm_sidecar=True, **options)
        self.assertEqual(refused['error'], 'destination_exists')
        self.assertEqual(self.image.read_bytes(), original)
        self.assertTrue(self.request('discard', session=options['session'])['result']['closed'])

    def test_create_save_reformat_and_discard(self):
        original = self.image.read_bytes()
        created = self.request('new_image', format='ibm-3740', confirmed=True)
        self.assertTrue(created['ok'], created)
        target = self.directory / 'created.raw'
        options = dict(session=created['result']['session'], destination=str(target))
        plan = self.request('save_plan', **options)
        self.assertTrue(plan['ok'], plan)
        result = self.request('save', **options, plan=plan['result'], confirm_sidecar=True)
        self.assertTrue(result['ok'], result)
        self.assertEqual(target.stat().st_size, 256256)
        self.assertEqual(self.image.read_bytes(), original)
        tools = Path(os.environ['CPMIMAGE_UPSTREAM_TOOLS'])
        (self.directory / 'diskdefs').write_bytes(target.with_suffix('.FDF').read_bytes())
        payload = self.directory / 'probe.bin'
        payload.write_bytes(bytes(range(128)))
        for args in [(str(payload), '0:PROBE.BIN'), ('0:PROBE.BIN', 'out.bin')]:
            subprocess.run([tools / 'cpmcp', '-f', 'ibm-3740', target, *args],
                           cwd=self.directory, check=True)
        self.assertEqual((self.directory / 'out.bin').read_bytes(), payload.read_bytes())
        populated = target.read_bytes()
        opened = self.request('open', image=str(target), format='ibm-3740', container='raw')
        token = opened['result']['session']
        formatted = self.request('format_image', session=token, confirmed=True)
        self.assertTrue(formatted['ok'], formatted)
        self.assertEqual(formatted['result']['entries'], [])
        self.assertEqual(target.read_bytes(), populated)
        discarded = self.request('discard', session=token)
        self.assertTrue(discarded['ok'], discarded)
        self.assertEqual(discarded['result']['entries'][0]['name'], '00PROBE.BIN')


if __name__ == '__main__':
    unittest.main()
