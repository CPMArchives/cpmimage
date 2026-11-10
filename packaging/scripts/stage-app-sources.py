# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage portable source inputs for native Windows/Linux development validation."""
import argparse,json,tarfile,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--helper',type=Path,help='Optional cross-built Windows read-only helper to stage with development sources');a=p.parse_args()
r=Path(__file__).resolve().parents[2];base=r/'packaging';(base/'downloads').mkdir(exist_ok=True)
files=[*r.joinpath('prototype/src').glob('*'),*r.joinpath('prototype/tests').glob('*.py'),*r.joinpath('prototype/tests').glob('*.cpp'),*r.joinpath('prototype/tests').glob('*.c')]
files += [r/x for x in ['prototype/CMakeLists.txt','prototype/broker.py','prototype/helper_guard.py','prototype/LICENSE','prototype/LICENSE-NOTICE.txt','experiments/process-comparison/adapters.py','experiments/process-comparison/windows_supervisor.py','fixtures/diskdefs','fixtures/ibm3740-independent.raw','fixtures/ibm3740-truncated.raw','fixtures/ibm3740-empty.raw','fixtures/ampro200-independent.dsk','fixtures/final-track-manifest.json']]
files += [r/'fixtures'/x['file'] for x in json.loads((r/'fixtures/final-track-manifest.json').read_text())]
with zipfile.ZipFile(base/'downloads/cpmimage-msvc-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for f in files:z.write(f,Path('cpmimage-feasibility')/f.relative_to(r))
    if a.helper:z.write(a.helper,'cpmimage-feasibility/prototype/build-windows/helper-readonly.exe')
with tarfile.open(base/'downloads/cpmimage-package-source.tar.gz','w:gz') as z:
    for f in files:z.add(f,arcname=Path('cpmimage-feasibility')/f.relative_to(r))
