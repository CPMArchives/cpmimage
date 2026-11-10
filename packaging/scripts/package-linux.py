# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import os,shutil,subprocess
from notices import BASE,ROOT,create
out=BASE/'dist/CPMImage-linux-arm64'
if out.exists():shutil.rmtree(out)
for sub in ['bin','lib','libexec','share/cpmimage','plugins']: (out/sub).mkdir(parents=True,exist_ok=True)
shutil.copy2(BASE/'app-linux/cpmimage-review',out/'bin/cpmimage')
qt=BASE/'qt-linux/install'
for stem in ['Core','Gui','Widgets','XcbQpa','OpenGL']:
 for f in (qt/'lib').glob('libQt6'+stem+'.so*'):
  if f.is_symlink(): (out/'lib'/f.name).symlink_to(os.readlink(f))
  elif f.is_file(): shutil.copy2(f,out/'lib'/f.name)
for group in ['platforms','xcbglintegrations','imageformats','platformthemes','styles']:
 if group=='platforms':
  (out/'plugins/platforms').mkdir(exist_ok=True)
  for name in ['libqxcb.so','libqoffscreen.so']:shutil.copy2(qt/'plugins/platforms'/name,out/'plugins/platforms'/name)
 elif (qt/'plugins'/group).exists():shutil.copytree(qt/'plugins'/group,out/'plugins'/group,symlinks=True)
helper=ROOT/'prototype/build/helper-readonly';shutil.copy2(helper,out/'libexec/cpmimage-helper')
for f in (ROOT/'prototype/build/deps/lib').glob('libdsk.so*'):
 if f.is_symlink(): (out/'lib'/f.name).symlink_to(os.readlink(f))
 else: shutil.copy2(f,out/'lib'/f.name)
runtime=out/'runtime';(runtime/'bin').mkdir(parents=True);(runtime/'lib').mkdir()
py=BASE/'python-linux';shutil.copy2(py/'bin/python3.13',runtime/'bin/python3.13')
for f in (py/'lib').glob('libpython*.so*'):
 if f.is_symlink(): (runtime/'lib'/f.name).symlink_to(os.readlink(f))
 else:shutil.copy2(f,runtime/'lib'/f.name)
shutil.copytree(py/'lib/python3.13',runtime/'lib/python3.13',symlinks=True,ignore=shutil.ignore_patterns('__pycache__','test','config-*'))
for rel in ['prototype/broker.py','prototype/helper_guard.py','experiments/process-comparison/adapters.py','experiments/process-comparison/windows_supervisor.py','fixtures/diskdefs']:
 dst=out/'share/cpmimage'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/rel,dst)
(out/'bin/qt.conf').write_text('[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\n')
launcher=out/'CPMImage';launcher.write_text('''#!/bin/sh
HERE=$(CDPATH= cd -- "${0%/*}" && pwd)
export QT_PLUGIN_PATH="$HERE/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/plugins/platforms"
unset QT_QPA_PLATFORMTHEME QT_STYLE_OVERRIDE
exec "$HERE/bin/cpmimage" "$@"
''');launcher.chmod(0o755)
for f in out.rglob('*'):
 if not f.is_file() or f.is_symlink():continue
 with f.open('rb') as stream:magic=stream.read(4)
 if magic!=b'\x7fELF':continue
 if f==out/'bin/cpmimage' or f==out/'libexec/cpmimage-helper':rpath='$ORIGIN/../lib'
 elif f==runtime/'bin/python3.13':rpath='$ORIGIN/../lib'
 elif out/'plugins' in f.parents:rpath='$ORIGIN/../../lib'
 else:rpath='$ORIGIN'
 subprocess.run(['patchelf','--set-rpath',rpath,str(f)],check=True)
create(out/'share/licenses/cpmimage','linux')
print(out)
