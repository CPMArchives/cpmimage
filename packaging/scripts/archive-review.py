# SPDX-License-Identifier: GPL-3.0-or-later
"""Archive tracked source plus exact upstream sources; never include compiler payloads."""
import argparse,hashlib,json,os,shutil,stat,subprocess,tarfile,zipfile
from pathlib import Path
BASE=Path(__file__).resolve().parents[1];ROOT=BASE.parent
p=argparse.ArgumentParser();p.add_argument('--source-only',action='store_true');a=p.parse_args()
subprocess.run(['git','diff','--quiet','HEAD','--'],cwd=ROOT,check=True)
tracked=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0');tracked=[x for x in tracked if x]
if not tracked:raise SystemExit('Archive a reviewed Git checkout, not an untracked build tree')
source=BASE/'source-companion/cpmimage'
if source.parent.exists():shutil.rmtree(source.parent)
source.mkdir(parents=True)
def copy(src,rel):
    dest=source/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest)
for name in tracked:copy(ROOT/name,name)
records=json.loads((BASE/'dependencies.json').read_text())
for item in records:
    if item['group'] not in ['backend','runtime']:continue
    src=ROOT/item['path']
    if not src.is_file() or hashlib.sha256(src.read_bytes()).hexdigest()!=item['sha256']:raise SystemExit('Fetch matching source inputs first: '+item['path'])
    copy(src,item['path'])
# Preserve available generated source/build configuration and SBOM material.
for platform in ['macos','linux']:
    for rel in ['build/config.summary','build/CMakeCache.txt','install/sbom/qtbase-6.12.0.spdx']:
        f=BASE/('qt-'+platform)/rel
        if f.is_file():copy(f,Path('packaging/build-records')/platform/rel)
for platform in ['macos','linux']:
    f=BASE/('build-python-'+platform)/'Python-3.13.17/Modules/Setup.local'
    if f.is_file():copy(f,Path('packaging/build-records')/platform/'Python-Modules-Setup.local')
if (BASE/'qt-msvc/sbom').is_dir():shutil.copytree(BASE/'qt-msvc/sbom',source/'packaging/build-records/windows/qt-sbom')
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip();(source/'SOURCE-REVISION.txt').write_text(commit+'\n')
manifest={str(f.relative_to(source)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(source.rglob('*')) if f.is_file()}
(source/'source-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
dist=BASE/'dist';dist.mkdir(exist_ok=True)
with tarfile.open(dist/'CPMImage-corresponding-source.tar.gz','w:gz') as z:z.add(source,arcname='cpmimage')
def zip_tree(folder,destination):
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for f in sorted(folder.rglob('*')):
            rel=str(f.relative_to(folder.parent))
            if f.is_symlink():
                info=zipfile.ZipInfo(rel);info.create_system=3;info.external_attr=(stat.S_IFLNK|0o777)<<16;z.writestr(info,os.readlink(f).encode())
            elif f.is_file():z.write(f,rel)
if not a.source_only:
    for name in ['CPMImage-macOS-arm64','CPMImage-windows-arm64','CPMImage-linux-arm64']:
        if not (dist/name).is_dir():raise SystemExit('Missing platform bundle: '+name)
    with tarfile.open(dist/'CPMImage-linux-arm64.tar.gz','w:gz') as z:z.add(dist/'CPMImage-linux-arm64',arcname='CPMImage-linux-arm64')
    for name in ['CPMImage-macOS-arm64','CPMImage-windows-arm64']:zip_tree(dist/name,dist/(name+'.zip'))
archives=sorted(f for f in dist.iterdir() if f.is_file() and f.suffix in ['.gz','.zip'])
(dist/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\n' for f in archives))
for f in archives:print(f.name,f.stat().st_size)
