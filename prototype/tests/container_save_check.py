# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from catalogue_check import CatalogueCheck


class ContainerSaveCheck(unittest.TestCase):
    def test_saved_containers(self):
        tools = os.environ.get('CPMIMAGE_UPSTREAM_TOOLS')
        root = Path(os.environ['CPMIMAGE_ROOT'])
        for container in ('imd', 'jv3', 'dmk'):
            with self.subTest(container=container):
                broker = CatalogueCheck()
                broker.setUp()
                self.addCleanup(broker.doCleanups)
                self.addCleanup(broker.tearDown)
                fmt = 'ibm-3740'
                opened = broker.request('open', image=str(broker.image), format=fmt, container='raw')
                self.assertTrue(opened['ok'], opened)
                dst = broker.directory / ('converted.' + container)
                profile = dict(track_order=[[track, 0] for track in range(77)],
                               default=dict(mode=5, size=128, sectors=list(range(1, 27))))
                dst.with_name(dst.name + '.mapping.json').write_text(json.dumps(profile))
                args = dict(session=opened['result']['session'], destination=str(dst), container=container)
                plan = broker.request('save_plan', **args)
                self.assertTrue(plan['ok'], plan)
                saved = broker.request('save', plan=plan['result'], confirm_sidecar=True, **args)
                self.assertTrue(saved['ok'], saved)
                dst.with_name(dst.name + '.mapping.json').unlink()
                sidecar = dst.with_suffix('.FDF')
                saved_definition = sidecar.read_bytes()
                sidecar.unlink()
                original = dst.read_bytes()
                detected = broker.request('inspect', image=str(dst))
                self.assertTrue(detected['ok'], detected)
                self.assertIn(fmt, [row['id'] for row in detected['result']['candidates']])
                self.assertEqual(dst.read_bytes(), original)
                self.assertFalse(sidecar.exists())
                sidecar.write_bytes(saved_definition)
                reopened = broker.request('open', image=str(dst), format=fmt, container=container)
                self.assertTrue(reopened['ok'], reopened)
                token = reopened['result']['session']
                payload = broker.directory / 'known.bin'
                payload.write_bytes(bytes(range(128)))
                edit = broker.request('edit', action='import', session=token, name='00KNOWN.BIN',
                                      path=str(payload), confirmed=True)
                self.assertTrue(edit['ok'], edit)
                if container == 'imd':
                    comment = broker.request('imd_comment', session=token, comment='Interoperability check')
                    self.assertTrue(comment['ok'], comment)
                opts = dict(session=token)
                plan = broker.request('save_plan', **opts)
                saved = broker.request('save', plan=plan['result'], confirm_sidecar=True, **opts)
                self.assertTrue(saved['ok'], saved)
                reopened = broker.request('open', image=str(dst), format=fmt, container=container)
                self.assertTrue(reopened['ok'], reopened)
                if container == 'jv3':
                    original = dst.read_bytes()
                    variants = {
                        'damaged': original[:2] + bytes((original[2] | 8,)) + original[3:],
                        'deleted': original[:2] + bytes((original[2] | 32,)) + original[3:],
                        'duplicate': original[:3] + original[:3] + original[6:],
                        'truncated': original[:-1],
                        'differs': original[:2] + bytes((original[2] ^ 128,)) + original[3:],
                    }
                    for diagnostic, data in variants.items():
                        bad = dst.with_name(diagnostic + '.jv3')
                        bad.write_bytes(data)
                        if diagnostic == 'differs':
                            bad.with_name(bad.name + '.mapping.json').write_text(json.dumps(profile))
                        invalid = broker.request('open', image=str(bad), format=fmt, container=container)
                        self.assertEqual(invalid.get('error'), 'physical_mapping', invalid)
                        self.assertIn(diagnostic, invalid['message'].lower())
                        self.assertEqual(bad.read_bytes(), data)
                    reopened = broker.request('open', image=str(dst), format=fmt, container=container)
                    self.assertTrue(reopened['ok'], reopened)
                if container == 'imd':
                    self.assertEqual(reopened['result']['imd_comment'], 'Interoperability check')
                exported = broker.directory / 'broker.bin'
                result = broker.request('export', session=reopened['result']['session'],
                                        name='00KNOWN.BIN', destination=str(exported))
                self.assertTrue(result['ok'], result)
                self.assertEqual(exported.read_bytes(), payload.read_bytes())
                if not tools:
                    continue
                if container != 'dmk':
                    (broker.directory / 'diskdefs').write_bytes(dst.with_suffix('.FDF').read_bytes())
                    output = broker.directory / 'independent.bin'
                    subprocess.run([Path(tools) / 'cpmcp', '-f', fmt, '-T', container, dst,
                                    '0:known.bin', output], cwd=broker.directory, check=True)
                    self.assertEqual(output.read_bytes(), payload.read_bytes())
                else:
                    raw_path = broker.directory / 'decoded.raw'
                    args = dict(session=reopened['result']['session'], destination=str(raw_path), container='raw')
                    plan = broker.request('save_plan', **args)
                    self.assertTrue(plan['ok'], plan)
                    saved = broker.request('save', plan=plan['result'], confirm_sidecar=True, **args)
                    self.assertTrue(saved['ok'], saved)
                    (broker.directory / 'diskdefs').write_bytes(raw_path.with_suffix('.FDF').read_bytes())
                    output = broker.directory / 'independent.bin'
                    subprocess.run([Path(tools) / 'cpmcp', '-f', fmt, '-T', 'raw', raw_path,
                                    '0:known.bin', output], cwd=broker.directory, check=True)
                    self.assertEqual(output.read_bytes(), payload.read_bytes())



if __name__ == '__main__':
    unittest.main()
