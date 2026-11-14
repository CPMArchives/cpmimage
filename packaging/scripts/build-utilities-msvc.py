# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the pinned Windows console suite from extracted source trees.

Run in an ARM64 MSVC developer environment. Output is written to ROOT/bin.
"""
import os, subprocess, traceback, sys, shutil
from pathlib import Path

root = Path(sys.argv[1]).resolve()
src = root / 'cpmtools-2.23'
pdc = root / 'PDCurses-3.9'
scripts = Path(__file__).resolve().parent
subprocess.run([sys.executable, scripts / 'configure-utility-catalogue.py', src], check=True)
shutil.copyfile(scripts.parent / 'source/cpmtools-msvc-config.h', src / 'config.h')
device = src / 'device_posix.c'
text = device.read_text()
if 'open(filename,mode|O_BINARY)' not in text:
    original = 'open(filename,mode)'
    if text.count(original) != 1:
        raise ValueError('Unexpected cpmtools device source')
    device.write_text(text.replace(original, 'open(filename,mode|O_BINARY)'))
out=root/'bin'
out.mkdir(exist_ok=True)
with (root/'build.log').open('w') as log:
 def cl(*args):
  subprocess.run(['cl.exe','/nologo','/O2','/MD','/D_CRT_SECURE_NO_WARNINGS',*map(str,args)],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
 try:
  cl('/c','/I'+str(pdc),*sorted((pdc/'pdcurses').glob('[a-z]*.c')),*sorted((pdc/'wincon').glob('[a-z]*.c')))
  pdc_objects=[out/(p.stem+'.obj') for group in ('pdcurses','wincon') for p in sorted((pdc/group).glob('[a-z]*.c'))]
  cl('/c','/I'+str(src),'/I'+str(pdc),
     *[src/(n+'.c') for n in ('cpmfs','device_posix','getopt','getopt1','term_curses')])
  for name in ('cpmls','cpmrm','cpmcp','cpmchmod','cpmchattr','mkfs.cpm','fsck.cpm','fsed.cpm'):
   extra=[out/'term_curses.obj',*pdc_objects,'user32.lib','advapi32.lib'] if name=='fsed.cpm' else []
   cl('/I'+str(src),'/Fe'+name+'.exe',src/(name+'.c'),
      *[out/(n+'.obj') for n in ('cpmfs','device_posix','getopt','getopt1')],*extra)
  (root/'build.exit').write_text('0')
 except Exception:
  log.write(traceback.format_exc())
  raise
