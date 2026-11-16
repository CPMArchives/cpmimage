# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import os,shutil,subprocess,plistlib,re,tempfile
from notices import BASE,ROOT,create
from utility_bundle import install_utilities
from editor_bundle import install_validator, stage_editor
app=BASE/'dist/CPMImage-macOS-arm64/CPMImage.app'
if app.exists():shutil.rmtree(app)
shutil.copytree(BASE/'app-macos/cpmimage-review.app',app,symlinks=True)
info=app/'Contents/Info.plist'
with info.open('rb') as stream: metadata=plistlib.load(stream)
old_executable = app/'Contents/MacOS'/metadata['CFBundleExecutable']
old_executable.rename(old_executable.with_name('cpmimage'))
icon = 'cpmimage.icns'
if not (app/'Contents/Resources'/icon).is_file():
 raise SystemExit(f'Missing macOS app icon: {icon}')
metadata.update(CFBundleExecutable='cpmimage',
                CFBundleIdentifier='org.cpmarchives.cpmimage.review',
                CFBundleName='CPMImage',
                CFBundleShortVersionString='1.3',
                CFBundleVersion='1.3',
                CFBundleIconFile=icon,
                LSMinimumSystemVersion='14.4')
with info.open('wb') as stream: plistlib.dump(metadata,stream)
resources=app/'Contents/Resources';resources.mkdir(exist_ok=True)
backend=resources/'cpmimage'
archive_codecs = sorted(str(path.relative_to(ROOT)) for path in (ROOT/'prototype/archive_codecs').iterdir() if path.is_file())
for rel in ['prototype/archive_view.py', 'prototype/broker.py','prototype/catalogue.py','prototype/container_mapping.py','prototype/publication.py','prototype/source_ownership.py','prototype/helper_guard.py','experiments/process-comparison/adapters.py','experiments/process-comparison/windows_supervisor.py','fixtures/diskdefs','fixtures/diskdefs-metadata.json'] + archive_codecs:
 dst=backend/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/rel,dst)
stage_editor(backend)
helper=app/'Contents/Helpers/cpmimage-helper';helper.parent.mkdir(exist_ok=True)
shutil.copy2(BASE/'cpmimage-helper-macos',helper)
runtime=resources/'runtime';shutil.copytree(BASE/'python-macos',runtime,symlinks=True,ignore=shutil.ignore_patterns('__pycache__'))
validator = install_validator(runtime, 'macos-arm64')
for f in [runtime/'bin/idle3',runtime/'bin/idle3.13',runtime/'bin/pydoc3',runtime/'bin/pydoc3.13',runtime/'bin/python3-config',runtime/'bin/python3.13-config']:
 if f.exists() or f.is_symlink():f.unlink()
# The interpreter discovers stdlib relative to itself; eliminate its build-prefix dylib reference.
py=runtime/'bin/python3.13';lib=runtime/'lib/libpython3.13.dylib'
# Reused private runtimes can retain their original build-prefix reference.
for line in subprocess.check_output(['otool','-L',str(py)],text=True).splitlines()[1:]:
 dependency=line.strip().split(' (')[0]
 if dependency.startswith('/') and dependency.endswith('/libpython3.13.dylib'):
  subprocess.run(['install_name_tool','-change',dependency,'@loader_path/../lib/libpython3.13.dylib',str(py)],check=True)
subprocess.run(['install_name_tool','-id','@rpath/libpython3.13.dylib',str(lib)],check=True)
frameworks=app/'Contents/Frameworks';frameworks.mkdir(exist_ok=True)
shutil.copy2(str(BASE/'helper-deps-macos/lib/libdsk.3.dylib'),frameworks/'libdsk.3.dylib')
subprocess.run(['install_name_tool','-change',str(BASE/'helper-deps-macos/lib/libdsk.3.dylib'),'@executable_path/../Frameworks/libdsk.3.dylib',str(helper)],check=True)
subprocess.run(['install_name_tool','-id','@rpath/libdsk.3.dylib',str(frameworks/'libdsk.3.dylib')],check=True)
subprocess.run([str(BASE/'qt-macos/install/bin/macdeployqt'),str(app),'-no-strip','-no-codesign','-verbose=2'],check=True)
# Retain the headless platform plugin for package diagnostics and automated QA.
platforms=app/'Contents/PlugIns/platforms'
shutil.copy2(BASE/'qt-macos/install/plugins/platforms/libqoffscreen.dylib',platforms/'libqoffscreen.dylib')
subprocess.run(['install_name_tool','-add_rpath','@loader_path/../../Frameworks',str(platforms/'libqoffscreen.dylib')],check=True)
install_utilities(app/'Contents/MacOS/cpmtools', 'macos')
create(resources/'notices','macos',validator)
# Remove stale build-prefix fallback search paths, even when the relocated app
# already works via its bundle-relative primary search path.
for f in app.rglob('*'):
 if not f.is_file() or f.is_symlink():continue
 with f.open('rb') as stream: header=stream.read(4)
 if header not in [b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca']:continue
 commands=subprocess.check_output(['otool','-l',str(f)],text=True)
 for path in re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset',commands):
  if path.startswith('/'):
   subprocess.run(['install_name_tool','-delete_rpath',path,str(f)],check=True)
# Sign only local review artifacts, without a developer account or network access.
for f in sorted(app.rglob('*'),key=lambda p:len(p.parts),reverse=True):
 if f.is_file() and not f.is_symlink() and f.open('rb').read(4) in [b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca']:
  subprocess.run(['codesign','--force','--sign','-',str(f)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
subprocess.run(['codesign','--force','--deep','--sign','-',str(app)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
with tempfile.TemporaryDirectory(prefix='cpmimage-dmg-') as temp:
 staging = Path(temp)
 shutil.copytree(app, staging/app.name, symlinks=True)
 (staging/'Applications').symlink_to('/Applications')
 dmg = app.parent.parent/'CPMImage-macOS-arm64.dmg'
 subprocess.run(['hdiutil','create','-ov','-format','UDZO','-volname','CPMImage',
                 '-srcfolder',str(staging),str(dmg)],check=True)
print(dmg)
