# SPDX-License-Identifier: GPL-3.0-or-later
import json,subprocess
from pathlib import Path
from notices import BASE
app=BASE/'dist/CPMImage-macOS-arm64/CPMImage.app';rows=[]
magic={b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca'}
for f in app.rglob('*'):
 if not f.is_file() or f.is_symlink():continue
 with f.open('rb') as stream:header=stream.read(4)
 if header not in magic:continue
 deps=subprocess.check_output(['otool','-L',str(f)],text=True).splitlines()[1:]
 dependencies=[d.strip().split(' (')[0] for d in deps]
 assert not any(d.startswith('/opt/homebrew') or '.chatgpt-projects' in d for d in dependencies),(f,dependencies)
 load='\n'.join(subprocess.check_output(['otool','-l',str(f)],text=True).splitlines()[1:])
 assert '/opt/homebrew' not in load and '.chatgpt-projects' not in load,(f,'Homebrew load command')
 rows.append(dict(path=str(f.relative_to(app)),dependencies=dependencies))
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
(BASE/'results/macos-binary-audit.json').write_text(json.dumps(rows,indent=2)+'\n')
print('Mach-O files audited',len(rows))
