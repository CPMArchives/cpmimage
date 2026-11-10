# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise a relocated package with an empty PATH and adversarial Python env vars."""
import argparse,hashlib,json,os,shutil,subprocess,tempfile,queue,threading
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('executable',type=Path);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--final-track-manifest',type=Path);args=p.parse_args()
exe=args.executable.resolve();baseline=args.fixture.read_bytes()
def digest(data):return hashlib.sha256(data).hexdigest()
def inventory(root):return {str(f.relative_to(root)):digest(f.read_bytes()) for f in root.rglob('*') if f.is_file() and not f.is_symlink()}
# Resolve bundle paths using the actual packaged launcher and shared Qt libraries.
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',PYTHONPATH='/nonexistent/poison',PYTHONHOME='/nonexistent/poison')
for key in ['QT_PLUGIN_PATH','QML2_IMPORT_PATH','DYLD_LIBRARY_PATH','LD_LIBRARY_PATH','QT_QPA_PLATFORMTHEME','QT_STYLE_OVERRIDE']:env.pop(key,None)
env['PATH']=str(Path(os.environ.get('SystemRoot','C:/Windows'))/'System32') if os.name=='nt' else '/nonexistent'
r=subprocess.run([str(exe),'--diagnostics'],env=env,capture_output=True,text=True,encoding="utf-8",cwd=tempfile.gettempdir(),timeout=15)
assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
paths=json.loads(r.stdout);assert paths['complete'],paths
root=Path(paths['root']).resolve();python=Path(paths['python']).resolve();helper=Path(paths['helper']).resolve();notices=Path(paths['notices']).resolve()
assert (notices/'THIRD-PARTY-NOTICES.txt').is_file()
package=exe.parent if os.name=='nt' else exe.parents[2] if '.app/' in str(exe) else exe.parent.parent if exe.parent.name=='bin' else exe.parent
before=inventory(package);final_tracks=[]
with tempfile.TemporaryDirectory(prefix='cpmimage-export-') as temp:
 d=Path(temp);image=d/'input ü.raw';image.write_bytes(baseline)
 proc=subprocess.Popen([str(python),'-I','-B','-u',str(root/'prototype/broker.py'),'--root',str(root),'--helper',str(helper)],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding="utf-8",cwd=tempfile.gettempdir())
 replies=queue.Queue()
 threading.Thread(target=lambda: [replies.put(line) for line in proc.stdout],daemon=True).start()
 def request(op,**values):
  proc.stdin.write(json.dumps(dict(v=1,id='package-check',op=op,**values))+'\n');proc.stdin.flush();return json.loads(replies.get(timeout=15))
 try:
  listing=request('open',image=str(image),format='ibm-3740',container='raw');assert listing['ok'],listing
  assert len(listing['result']['entries'])==3,listing
  token=listing['result']['session'];expected={'00binary.bin':'110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b','07binary.bin':'cd6816b77f68d70001fc3eaa4d42bdd67cb5973b3151cc5292ecc02a3daac6ab','15tail.bin':'e92de1c61e03ecf17afd276aa6bad358b8b2cad5fe4e607a42152df1befd5a6f'}
  exports=[]
  for name,sha in expected.items():
   dest=d/(name+' ü export');reply=request('export',session=token,name=name,destination=str(dest));assert reply['ok'],reply
   assert digest(dest.read_bytes())==sha,(name,reply)
   refusal=request('export',session=token,name=name,destination=str(dest));assert refusal['error']=='destination_exists',refusal
   exports.append(dict(name=name,size=len(dest.read_bytes()),sha256=sha))
  assert image.read_bytes()==baseline
  for n in range(20):
   reply=request('export',session=token,name='00binary.bin',destination=str(d/str(n)));assert reply['ok'],reply
  if args.final_track_manifest:
   manifest=args.final_track_manifest.resolve()
   for index,fixture in enumerate(json.loads(manifest.read_text())):
    original=(manifest.parent/fixture['file']).read_bytes();target=d/('final-track-'+str(index)+'.raw');target.write_bytes(original)
    listing=request('open',image=str(target),format=fixture['format'],container='raw');assert listing['ok'],listing
    destination=d/('last-track-export-'+str(index));reply=request('export',session=listing['result']['session'],name='00last.bin',destination=str(destination));assert reply['ok'],reply
    assert digest(destination.read_bytes())==fixture['export_sha256']
    assert target.read_bytes()==original
    final_tracks.append(dict(fixture=fixture['file'],sha256=digest(destination.read_bytes()),source_unchanged=True))
 finally:
  proc.stdin.close();proc.wait(timeout=15);assert proc.returncode==0,proc.stderr.read()
  proc.stdout.close();proc.stderr.close()
assert inventory(package)==before,'Package contents changed while running (possible bytecode cache/signature mutation)'
args.report.parent.mkdir(parents=True,exist_ok=True)
args.report.write_text(json.dumps(dict(diagnostics=paths,exports=exports,rapid_exports=20,final_track_cases=final_tracks,source_unchanged=True,package_unchanged=True,python_environment_ignored=True),indent=2)+'\n')
print(args.report)
