# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess
import unittest

from catalogue_check import CatalogueCheck


class EditCheck(unittest.TestCase):
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
