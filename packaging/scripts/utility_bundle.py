# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage the host-native utility suites for the dispatcher."""
import os
from pathlib import Path
import shutil

UTILITIES = ('cpmls', 'cpmrm', 'cpmcp', 'cpmchmod', 'cpmchattr',
             'mkfs.cpm', 'fsck.cpm', 'fsed.cpm')

LIBDSK_UTILITIES = ('dsktrans', 'dskform', 'dskid', 'dskdump', 'dskscan', 'dskutil', 'md3serial', 'apriboot')
IMAGEDISK_UTILITIES = ('imda', 'imdchk', 'imdu', 'imdcmp', 'bin2imd', 'imdv')


def install_utilities(directory, platform):
    for name, variable, utilities in (
        ('cpmtools', 'CPMIMAGE_CPMTOOLS_BIN', UTILITIES),
        ('libdsk', 'CPMIMAGE_LIBDSK_BIN', LIBDSK_UTILITIES),
        ('imagedisk', 'CPMIMAGE_IMAGEDISK_BIN', IMAGEDISK_UTILITIES),
    ):
        install_suite(directory / name, platform, variable, utilities)


def install_suite(destination, platform, variable, utilities):
    source = os.environ.get(variable)
    if not source:
        raise SystemExit(f'Set {variable} to the built {destination.name} utility directory.')
    suffix = '.exe' if platform == 'windows' else ''
    executables = [Path(source) / (name + suffix) for name in utilities]
    missing = [file.name for file in executables if not file.is_file()]
    if missing:
        raise SystemExit(f'Incomplete {destination.name} suite: ' + ', '.join(missing))
    destination.mkdir(parents=True, exist_ok=True)
    for file in executables:
        shutil.copy2(file, destination / file.name)
    if platform == 'windows':
        for file in Path(source).glob('*.dll'):
            shutil.copy2(file, destination / file.name)
