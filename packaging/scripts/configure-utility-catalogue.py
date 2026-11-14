#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add an explicit catalogue override to the standalone cpmtools build."""
from pathlib import Path
import sys

source = Path(sys.argv[1]) / 'cpmfs.c'
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
