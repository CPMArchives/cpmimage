# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression at real helper/broker boundaries, including final physical sector."""
import argparse,hashlib,json,os,platform,subprocess,sys,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--helper',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--expect-failure',action='store_true');a=p.parse_args()
r=a.root.resolve();helper=a.helper.resolve();manifest=json.loads((r/'fixtures/final-track-manifest.json').read_text());rows=[]
for fixture in manifest:
 image=r/'fixtures'/fixture['file'];before=image.read_bytes()
 with tempfile.TemporaryDirectory(prefix='cpm-final-track-') as td:
  dest=Path(td)/'last.bin'
  x=subprocess.run([str(helper),'export',str(image),fixture['format'],'raw','00last.bin',str(dest)],cwd=r/'fixtures',capture_output=True,text=True,timeout=15)
  result=json.loads(x.stdout);success=x.returncode==0 and result['ok']
  if a.expect_failure:
   assert success==fixture['expected_before_success'],(fixture,result)
   if success:assert hashlib.sha256(dest.read_bytes()).hexdigest()==fixture['export_sha256']
  else:
   assert success,(fixture,result)
   assert len(dest.read_bytes())==fixture['export_bytes']
   assert hashlib.sha256(dest.read_bytes()).hexdigest()==fixture['export_sha256']
   proc=subprocess.Popen([sys.executable,'-I','-B','-u',str(r/'prototype/broker.py'),'--root',str(r),'--helper',str(helper)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   def request(op,**kw):
    proc.stdin.write(json.dumps(dict(v=1,id='final-track',op=op,**kw))+'\n');proc.stdin.flush();return json.loads(proc.stdout.readline())
   try:
    opened=request('open',image=str(image),format=fixture['format'],container='raw');assert opened['ok'],opened
    assert opened['result']['unused_tail_filled']==fixture['trimmed']
    dest.unlink();exported=request('export',session=opened['result']['session'],name='00last.bin',destination=str(dest));assert exported['ok'],exported
    assert hashlib.sha256(dest.read_bytes()).hexdigest()==fixture['export_sha256']
   finally:
    proc.stdin.close();proc.wait(timeout=15);assert proc.returncode==0,proc.stderr.read();proc.stdout.close();proc.stderr.close()
  assert image.read_bytes()==before
  rows.append(dict(fixture=fixture['file'],helper_success=success,response=result,expected_sha256=fixture['export_sha256'],source_unchanged=True,broker_passed=not a.expect_failure))
a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(dict(platform=platform.platform(),python=sys.version,helper_sha256=hashlib.sha256(helper.read_bytes()).hexdigest(),expected_failure=a.expect_failure,cases=rows),indent=2)+'\n');print(a.report)
