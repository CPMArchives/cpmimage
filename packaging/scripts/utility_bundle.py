# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage the complete, host-native cpmtools suite for the dispatcher."""
import os
from pathlib import Path
import shutil

UTILITIES = ('cpmls', 'cpmrm', 'cpmcp', 'cpmchmod', 'cpmchattr',
             'mkfs.cpm', 'fsck.cpm', 'fsed.cpm')


def install_utilities(destination, platform):
    source = os.environ.get('CPMIMAGE_CPMTOOLS_BIN')
    if not source:
        raise SystemExit('Set CPMIMAGE_CPMTOOLS_BIN to the built cpmtools 2.23 utility directory.')
    suffix = '.exe' if platform == 'windows' else ''
    executables = [Path(source) / (name + suffix) for name in UTILITIES]
    missing = [file.name for file in executables if not file.is_file()]
    if missing:
        raise SystemExit('Incomplete cpmtools suite: ' + ', '.join(missing))
    destination.mkdir(parents=True, exist_ok=True)
    for file in executables:
        shutil.copy2(file, destination / file.name)
    if platform == 'windows':
        for file in Path(source).glob('*.dll'):
            shutil.copy2(file, destination / file.name)
