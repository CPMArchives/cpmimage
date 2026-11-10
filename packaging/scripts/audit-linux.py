# SPDX-License-Identifier: GPL-3.0-or-later
import json,subprocess
from pathlib import Path
from notices import BASE
root=BASE/'dist/CPMImage-linux-arm64';rows=[]
for f in root.rglob('*'):
 if not f.is_file() or f.is_symlink():continue
 with f.open('rb') as stream:magic=stream.read(4)
 if magic!=b'\x7fELF':continue
 paths=subprocess.check_output(['patchelf','--print-rpath',str(f)],text=True).strip()
 assert not any(path.startswith('/') for path in paths.split(':') if path),(f,paths)
 result=subprocess.run(['ldd',str(f)],capture_output=True,text=True)
 assert 'not found' not in result.stdout,(f,result.stdout)
 for line in result.stdout.splitlines():
  if ('libQt6' in line or 'libdsk.so' in line or 'libpython' in line) and '=>' in line:
   target=line.split('=>')[1].strip().split(' (')[0]
   assert Path(target).resolve().is_relative_to(root.resolve()),(f,line)
 rows.append(dict(path=str(f.relative_to(root)),rpath=paths,dependencies=result.stdout))
(BASE/'results/linux-binary-audit.json').write_text(json.dumps(rows,indent=2)+'\n')
print('ELF files audited',len(rows))
