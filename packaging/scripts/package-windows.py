# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble the ARM64 review package from native MSVC build output."""
import argparse,shutil,zipfile
from pathlib import Path
from notices import BASE,ROOT,create
from utility_bundle import install_utilities
from editor_bundle import install_validator, stage_editor
p=argparse.ArgumentParser();p.add_argument('executable',type=Path)
p.add_argument('--arch', choices=('arm64', 'x86-64'), default='arm64')
p.add_argument('--qt', type=Path)
p.add_argument('--crt', type=Path)
p.add_argument('--helper', type=Path)
p.add_argument('--python-runtime', type=Path)
a=p.parse_args()
if a.arch == 'x86-64' and not all((a.qt, a.crt, a.helper, a.python_runtime)):
 p.error('x86-64 requires explicit --qt, --crt, --helper and --python-runtime inputs')
# Retain the input first, including when reassembling an existing package.
executable=a.executable.read_bytes()
out=BASE/'dist'/f'CPMImage-windows-{a.arch}'
if out.exists():shutil.rmtree(out)
out.mkdir(parents=True);(out/'cpmwb.exe').write_bytes(executable)
qt=a.qt or BASE/'qt-msvc'
for name in ['Core','Gui','Widgets']:shutil.copy2(qt/'bin'/('Qt6'+name+'.dll'),out)
for group,names in {'platforms':['qwindows.dll','qoffscreen.dll'],'styles':['qmodernwindowsstyle.dll']}.items():
 for name in names:
  f=qt/'plugins'/group/name
  if f.exists():
   d=out/'plugins'/group;d.mkdir(parents=True,exist_ok=True);shutil.copy2(f,d)
(out/'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=plugins\n')
crt=a.crt or BASE/'msvc-extracted/VC/Redist/MSVC/14.44.35112/arm64/Microsoft.VC143.CRT'
for f in crt.glob('*.dll'):shutil.copy2(f,out)
(out/'helpers').mkdir();shutil.copy2(a.helper or ROOT/'prototype/build-windows/helper-readonly.exe',out/'helpers/cpmimage-helper.exe')
runtime=out/'runtime';runtime.mkdir()
py=a.python_runtime or ROOT/'windows-runtime/offline-media/CPMImageTests/python'
for name in ['python.exe','python3.dll','python313.dll','python313.zip','python313._pth','LICENSE.txt','_ctypes.pyd','libffi-8.dll','select.pyd','unicodedata.pyd']:
 shutil.copy2(py/name,runtime/name)
for f in crt.glob('vcruntime*.dll'):shutil.copy2(f,runtime)
validator = install_validator(runtime, 'windows-' + a.arch)
archive_codecs = sorted(str(path.relative_to(ROOT)) for path in (ROOT/'prototype/archive_codecs').iterdir() if path.is_file())
for rel in ['prototype/archive_view.py', 'prototype/broker.py','prototype/catalogue.py','prototype/container_mapping.py','prototype/publication.py','prototype/source_ownership.py','prototype/helper_guard.py','experiments/process-comparison/adapters.py','experiments/process-comparison/windows_supervisor.py','fixtures/diskdefs','fixtures/diskdefs-metadata.json'] + archive_codecs:
 dst=out/'resources/cpmimage'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/rel,dst)
stage_editor(out/'resources/cpmimage')
install_utilities(out, 'windows')
create(out/'notices','windows',validator)
with zipfile.ZipFile(BASE/'downloads'/f'windows-package-{a.arch}.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for f in out.rglob('*'):
  if f.is_file():z.write(f,f.relative_to(out.parent))
print(out)
