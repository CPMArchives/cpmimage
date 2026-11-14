# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble the ARM64 review package from native MSVC build output."""
import argparse,shutil,zipfile
from pathlib import Path
from notices import BASE,ROOT,create
p=argparse.ArgumentParser();p.add_argument('executable',type=Path);a=p.parse_args()
# Retain the input first, including when reassembling an existing package.
executable=a.executable.read_bytes()
out=BASE/'dist/CPMImage-windows-arm64'
if out.exists():shutil.rmtree(out)
out.mkdir(parents=True);(out/'cpmimage.exe').write_bytes(executable)
qt=BASE/'qt-msvc'
for name in ['Core','Gui','Widgets']:shutil.copy2(qt/'bin'/('Qt6'+name+'.dll'),out)
for group,names in {'platforms':['qwindows.dll','qoffscreen.dll'],'styles':['qmodernwindowsstyle.dll']}.items():
 for name in names:
  f=qt/'plugins'/group/name
  if f.exists():
   d=out/'plugins'/group;d.mkdir(parents=True,exist_ok=True);shutil.copy2(f,d)
(out/'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=plugins\n')
crt=BASE/'msvc-extracted/VC/Redist/MSVC/14.44.35112/arm64/Microsoft.VC143.CRT'
for f in crt.glob('*.dll'):shutil.copy2(f,out)
(out/'helpers').mkdir();shutil.copy2(ROOT/'prototype/build-windows/helper-readonly.exe',out/'helpers/cpmimage-helper.exe')
runtime=out/'runtime';runtime.mkdir()
py=ROOT/'windows-runtime/offline-media/CPMImageTests/python'
for name in ['python.exe','python3.dll','python313.dll','python313.zip','python313._pth','LICENSE.txt','_ctypes.pyd','libffi-8.dll','select.pyd','unicodedata.pyd']:
 shutil.copy2(py/name,runtime/name)
for f in crt.glob('vcruntime*.dll'):shutil.copy2(f,runtime)
for rel in ['prototype/broker.py','prototype/catalogue.py','prototype/helper_guard.py','experiments/process-comparison/adapters.py','experiments/process-comparison/windows_supervisor.py','fixtures/diskdefs','fixtures/diskdefs-metadata.json']:
 dst=out/'resources/cpmimage'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/rel,dst)
create(out/'notices','windows')
with zipfile.ZipFile(BASE/'downloads/windows-package.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for f in out.rglob('*'):
  if f.is_file():z.write(f,f.relative_to(out.parent))
print(out)
