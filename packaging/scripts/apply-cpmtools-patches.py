# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply our recorded bridge fix to the pinned cpmtools2.23 build tree."""
import subprocess,sys
from pathlib import Path
source=Path(sys.argv[1]).resolve();patch=Path(__file__).resolve().parents[1]/'source/cpmtools-2.23-odd-tracks-libdsk.patch'
if 'if (tracks <= 43 || (tracks % 2) != 0)' not in (source/'device_libdsk.c').read_text():
 subprocess.run(['patch','-p1','-i',str(patch)],cwd=source,check=True)

else:
 # Upgrade the comment on already-patched local build trees; no semantic change.
 target=source/'device_libdsk.c';text=target.read_text()
 text=text.replace('/* Preserve every logical track when probing cannot identify the layout.', '/* CPMImage local modification, 2026-11-10: preserve every logical track.')
 if text!=target.read_text():target.write_text(text)

# Only dedicated read-only cpmfs objects define this opt-in name policy.
if 'CPMIMAGE_READ_ONLY_NAMES' not in (source/'cpmdir.h').read_text():
 subprocess.run(['patch','-p1','-i',str(patch.parent/'cpmtools-2.23-readonly-names.patch')],cwd=source,check=True)

if 'cpmImageReadDefinition' not in (source/'cpmfs.c').read_text():
 subprocess.run(['patch','-p1','-i',str(patch.parent/'cpmtools-2.23-diskdefs.patch')],cwd=source,check=True)
