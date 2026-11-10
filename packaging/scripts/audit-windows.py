# SPDX-License-Identifier: GPL-3.0-or-later
import json,re,subprocess
from notices import BASE,ROOT
root=BASE/'dist/CPMImage-windows-arm64'
readobj=ROOT/'windows-runtime/llvm-mingw-20261104-ucrt-macos-universal/bin/llvm-readobj'
rows=[];system=set()
for f in root.rglob('*'):
 if not f.is_file() or f.suffix.lower() not in ['.exe','.dll','.pyd']:continue
 text=subprocess.check_output([str(readobj),'--file-headers','--coff-imports',str(f)],text=True)
 assert 'IMAGE_FILE_MACHINE_ARM64' in text,f
 imports=re.findall(r'^  Name: (\S+)',text,re.M)
 for dep in imports:
  if not any((folder/dep).exists() or any(x.name.lower()==dep.lower() for x in folder.glob('*')) for folder in [f.parent,root]):system.add(dep.lower())
 assert not any(x.lower().startswith(('qt6','python3','vcruntime','msvcp','libffi','libdsk','libstdc++','libgcc','libwinpthread')) and not any(y.name.lower()==x.lower() for y in root.rglob('*')) for x in imports),(f,imports)
 rows.append(dict(path=str(f.relative_to(root)),imports=imports))
(BASE/'results/windows-binary-audit.json').write_text(json.dumps(dict(binaries=rows,os_prerequisite_imports=sorted(system)),indent=2)+'\n')
print('ARM64 PE files audited',len(rows));print('OS imports:',', '.join(sorted(system)))
