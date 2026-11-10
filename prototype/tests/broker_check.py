# SPDX-License-Identifier: GPL-3.0-or-later
"""Integration checks use actual native helper and independent fixture oracles."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(os.environ['CPMIMAGE_ROOT'])
HELPER = Path(os.environ['CPMIMAGE_HELPER'])

class BrokerCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.image = self.directory / 'image ü.raw'
        shutil.copyfile(ROOT/'fixtures/ibm3740-independent.raw', self.image)
        self.before = self.image.read_bytes()
        self.proc = subprocess.Popen([sys.executable, '-u', str(ROOT/'prototype/broker.py'),
            '--root', str(ROOT), '--helper', str(HELPER)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    def tearDown(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=15)
        self.proc.stdout.close(); self.proc.stderr.close()
        self.temp.cleanup()
    def request(self, op, **values):
        self.proc.stdin.write(json.dumps(dict(v=1,id='test',op=op,**values))+'\n')
        self.proc.stdin.flush()
        return json.loads(self.proc.stdout.readline())
    def opened(self):
        reply = self.request('open',image=str(self.image),format='ibm-3740',container='raw')
        self.assertTrue(reply['ok'],reply)
        self.assertEqual(len(reply['result']['entries']),3)
        return reply['result']['session']
    def test_export_padding_and_no_clobber(self):
        token = self.opened(); destination = self.directory/'export ü.bin'
        reply = self.request('export',session=token,name='15tail.bin',destination=str(destination))
        self.assertTrue(reply['ok'],reply)
        self.assertEqual(len(destination.read_bytes()),256)
        self.assertEqual(hashlib.sha256(destination.read_bytes()).hexdigest(),
                         'e92de1c61e03ecf17afd276aa6bad358b8b2cad5fe4e607a42152df1befd5a6f')
        self.assertEqual(self.request('export',session=token,name='15tail.bin',destination=str(destination))['error'],'destination_exists')
        self.assertEqual(self.image.read_bytes(),self.before)
    def test_sequential_requests_never_report_busy(self):
        token = self.opened()
        for n in range(20):
            reply = self.request('export',session=token,name='00binary.bin',destination=str(self.directory/('rapid-%d.bin'%n)))
            self.assertTrue(reply['ok'],reply)
        self.assertEqual(self.image.read_bytes(),self.before)

    def test_trimmed_unused_tail_export_and_source_fingerprint(self):
        trimmed = self.before[:-128]
        self.image.write_bytes(trimmed)
        reply = self.request('open',image=str(self.image),format='ibm-3740',container='raw')
        self.assertTrue(reply['ok'], reply)
        self.assertTrue(reply['result']['unused_tail_filled'])
        self.assertEqual(reply['result']['source_sha256'], hashlib.sha256(trimmed).hexdigest())
        token = reply['result']['session']
        output = self.directory/'trimmed export.bin'
        exported = self.request('export',session=token,name='00binary.bin',destination=str(output))
        self.assertTrue(exported['ok'], exported)
        self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(),
                         '110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b')
        self.assertEqual(self.image.read_bytes(), trimmed)
        self.image.write_bytes(trimmed[:-1]+b'!')
        self.assertEqual(self.request('export',session=token,name='00binary.bin',
                                     destination=str(self.directory/'changed'))['error'],'source_changed')

    def test_read_only_cpm_slash_and_plus_names(self):
        for spelling, name in [(b'SIG/M   LIB', '00sig/m.lib'), (b'CP-M+   LBR', '00cp-m+.lbr')]:
            data = bytearray(self.before)
            found = False
            for offset in range(6656,9984,32):
                entry = data[offset:offset+32]
                if entry[0] == 0 and bytes(x&127 for x in entry[1:12]) == b'BINARY  BIN':
                    data[offset+1:offset+12] = bytes(x|(entry[i+1]&128) for i,x in enumerate(spelling))
                    found = True
            self.assertTrue(found)
            self.image.write_bytes(data)
            reply = self.request('open', image=str(self.image),format='ibm-3740',container='raw')
            self.assertTrue(reply['ok'],reply)
            self.assertIn(name,[e['name'] for e in reply['result']['entries']])
            dest = self.directory/('safe-'+str(len(name))+'.bin')
            if dest.exists():dest.unlink()
            exported = self.request('export',session=reply['result']['session'],name=name,destination=str(dest))
            self.assertTrue(exported['ok'],exported)
            self.assertEqual(hashlib.sha256(dest.read_bytes()).hexdigest(),
                             '110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b')
            self.assertEqual(self.image.read_bytes(),bytes(data))

    def test_missing_allocated_sector_rejected(self):
        # Directory sectors end by byte9984, but fixture files extend further.
        self.image.write_bytes(self.before[:9984])
        reply = self.request('open',image=str(self.image),format='ibm-3740',container='raw')
        self.assertFalse(reply['ok'], reply)
        self.assertEqual(reply['error'], 'image_truncated')
        self.assertEqual(self.image.read_bytes(), self.before[:9984])

    def test_partial_sector_and_oversize_rejected(self):
        for data in [self.before[:-1],self.before+b'\xe5'*128]:
            self.image.write_bytes(data)
            reply = self.request('open',image=str(self.image),format='ibm-3740',container='raw')
            self.assertFalse(reply['ok'],reply)
            self.assertEqual(reply['error'],'image_size')
            self.assertEqual(self.image.read_bytes(), data)

    def test_source_change_and_stale_session(self):
        token = self.opened(); self.image.write_bytes(self.before[:-1]+b'!')
        self.assertEqual(self.request('export',session=token,name='00binary.bin',destination=str(self.directory/'out'))['error'],'source_changed')
        self.assertEqual(self.request('export',session='old',name='00binary.bin',destination=str(self.directory/'out'))['error'],'stale_session')
        self.assertFalse((self.directory/'out').exists())
    def test_failed_open_invalidates_listing(self):
        token = self.opened()
        self.assertFalse(self.request('open',image=str(ROOT/'fixtures/ibm3740-truncated.raw'),format='ibm-3740',container='raw')['ok'])
        self.assertEqual(self.request('export',session=token,name='00binary.bin',destination=str(self.directory/'out'))['error'],'stale_session')
    def test_mutations_rejected(self):
        self.assertEqual(self.request('rename')['error'],'read_only')
        result = subprocess.run([str(HELPER),'rename',str(self.image),'ibm-3740','raw','00binary.bin','00new.bin'],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(json.loads(result.stdout)['error'],'read_only')
        self.assertEqual(self.image.read_bytes(),self.before)
    @unittest.skipIf(os.name == 'nt', 'POSIX orphan guard; Windows uses Job Objects')
    def test_broker_death_reaps_orphan_helper(self):
        self.proc.stdin.close(); self.proc.wait(timeout=5)
        self.proc.stdout.close(); self.proc.stderr.close()
        started = self.directory/'helper-started'
        escaped = self.directory/'helper-escaped'
        fake = self.directory/'orphan-helper'
        fake.write_text('#!/usr/bin/env python3\nimport pathlib,time\n'
            + 'pathlib.Path('+repr(str(started))+').touch()\ntime.sleep(2)\n'
            + 'pathlib.Path('+repr(str(escaped))+').touch()\n')
        fake.chmod(0o755)
        self.proc = subprocess.Popen([sys.executable,'-u',str(ROOT/'prototype/broker.py'),
            '--root',str(ROOT),'--helper',str(fake)],stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        self.proc.stdin.write(json.dumps(dict(v=1,id='orphan',op='open',image=str(self.image),
            format='ibm-3740',container='raw'))+'\n'); self.proc.stdin.flush()
        end = time.monotonic()+5
        while not started.exists() and time.monotonic()<end: time.sleep(.01)
        self.assertTrue(started.exists())
        self.proc.kill(); self.proc.wait(timeout=5)
        time.sleep(2.3)
        self.assertFalse(escaped.exists(), 'helper outlived killed broker')

    @unittest.skipIf(os.name == "nt", "POSIX script fixture; Windows process trees covered by Qt Job Object tests")
    def test_cancel_reaps_helper_and_recovers(self):
        # Deterministic fault at the helper boundary, not a slow production fixture.
        self.proc.stdin.close(); self.proc.wait(timeout=5)
        self.proc.stdout.close(); self.proc.stderr.close()
        marker = self.directory/'started'
        fake = self.directory/'slow-helper'
        fake.write_text('#!/usr/bin/env python3\nimport pathlib,time\n'
                        + 'pathlib.Path('+repr(str(marker))+').touch()\ntime.sleep(60)\n')
        fake.chmod(0o755)
        self.proc = subprocess.Popen([sys.executable,'-u',str(ROOT/'prototype/broker.py'),
            '--root',str(ROOT),'--helper',str(fake)],stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        request = dict(v=1,id='slow',op='open',image=str(self.image),format='ibm-3740',container='raw')
        self.proc.stdin.write(json.dumps(request)+'\n'); self.proc.stdin.flush()
        end = time.monotonic()+5
        while not marker.exists() and time.monotonic()<end: time.sleep(.01)
        self.assertTrue(marker.exists())
        self.proc.stdin.write(json.dumps(dict(v=1,id='cancel',op='cancel',target='slow'))+'\n')
        self.proc.stdin.flush()
        self.assertEqual(json.loads(self.proc.stdout.readline())['error'],'cancelled')
        # Replace test helper with the real executable and confirm another request works.
        shutil.copyfile(HELPER,fake)
        self.opened()
        self.assertEqual(self.image.read_bytes(),self.before)

if __name__ == '__main__':
    unittest.main(verbosity=2)
