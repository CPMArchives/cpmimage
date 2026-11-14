#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add an explicit catalogue override to the standalone cpmtools build and apply formatter fixes."""
from pathlib import Path
import subprocess
import sys

tree = Path(sys.argv[1]).resolve()
patches = Path(__file__).resolve().parents[1] / 'source'
for name, target, marker in (
    ('bootsec-assert', 'cpmfs.c', 'assert(d->boottrk>=0 || d->bootsec>=0)'),
    ('mkfs-addressing', 'mkfs.cpm.c', 'CPMIMAGE_FORMAT_HELPER'),
):
    if marker not in (tree / target).read_text():
        subprocess.run(['patch', '-p1', '-i', str(patches / f'cpmtools-2.23-{name}.patch')],
                       cwd=tree, check=True)

source = tree / 'cpmfs.c'
text = source.read_text()
marker = '  const char *catalogue = getenv("CPMTOOLS_DISKDEFS");'
if marker not in text:
    original = '  if ((fp=fopen("diskdefs","r"))==(FILE*)0 && (fp=fopen(DISKDEFS,"r"))==(FILE*)0)'
    assert text.count(original) == 1
    text = text.replace(original, marker + '''
  if (catalogue && *catalogue)
  {
    if ((fp=fopen(catalogue,"r"))==(FILE*)0)
    {
      fprintf(stderr,"%s: Cannot open diskdefs: %s\\n",cmd,catalogue);
      exit(1);
    }
  }
  else''' + '\n' + original)
    source.write_text(text)
