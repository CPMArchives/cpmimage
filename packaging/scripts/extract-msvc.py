# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract verified official toolchain payloads; does not accept vendor terms.
Run fetch-msvc.py/fetch-sdk-cabs.py first. Requires msitools for SDK archives.
"""
from pathlib import Path
import zipfile,subprocess
b=Path(__file__).resolve().parents[1]
for archive in (b/'downloads/msvc').glob('*.vsix'):
 with zipfile.ZipFile(archive) as z:
  for item in z.infolist():
   name=item.filename.replace('\\','/')
   if name.startswith('Contents/') and not item.is_dir():
    rel=Path(name[len('Contents/'):]);assert '..' not in rel.parts
    out=b/'msvc-extracted'/rel;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(z.read(item))
(b/'sdk-extracted').mkdir(exist_ok=True)
for archive in (b/'downloads/msvc/Installers').glob('*.msi'):
 subprocess.run(['msiextract','-C',str(b/'sdk-extracted'),str(archive)],check=True)
