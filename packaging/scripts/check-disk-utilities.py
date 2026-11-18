# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke-test the new utility suites through a packaged Workbench executable."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

from utility_bundle import IMAGEDISK_UTILITIES, LIBDSK_UTILITIES

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('workbench', type=Path)
parser.add_argument('teledisk', type=Path, help='A TD0 that standalone dskid can identify')
args = parser.parse_args()
executable = args.workbench.resolve()
original = args.teledisk.resolve().read_bytes()
env = dict(os.environ, QT_QPA_PLATFORM='missing-platform')


def run(directory, *arguments):
    return subprocess.run([executable, *arguments], cwd=directory, env=env,
                          capture_output=True, timeout=30, check=False)


with tempfile.TemporaryDirectory() as temp:
    directory = Path(temp)
    source = directory / 'TD0 disk image.td0'
    source.write_bytes(original)
    result = run(directory, '/dskid', '-itype', 'tele', source.name)
    assert result.returncode == 0 and b'TeleDisk file driver' in result.stdout, result
    explicit = run(directory, '/dskid', '-type', 'tele', source.name)
    assert explicit.returncode == 0 and explicit.stdout == result.stdout, explicit
    assert source.read_bytes() == original
    print('dskid TD0:', hashlib.sha256(original).hexdigest())
    result = run(directory, '/dskid', '-type', 'tele', 'missing.td0')
    assert result.returncode == 1 and b'missing.td0' in result.stderr, result
    for utility in LIBDSK_UTILITIES:
        result = run(directory, '/' + utility, '-types')
        assert result.returncode == 0 and b'TeleDisk' in result.stdout, result
        print(utility, 'passed')
    for utility in IMAGEDISK_UTILITIES:
        result = run(directory, '/' + utility, '--help')
        assert b'usage:' in (result.stdout + result.stderr).lower() and result.returncode in (0, 1), result
        print(utility, 'passed')
    # Mode 5, cylinder/head 0, one 128-byte sector stored as a fill byte.
    (directory / 'Sample disk.imd').write_bytes(
        b'IMD 1.18: 27/07/2014 01:37:52\r\nfixture\x1a' + bytes((5, 0, 0, 1, 0, 1, 2, 0xE5)))
    result = run(directory, '/imda', 'Sample disk.imd')
    assert result.returncode == 0 and b'250kbps' in result.stdout, result
    print('IMDA analysis passed')
    result = run(directory, '/dskid', '/imda')
    assert result.returncode == 2 and b'Choose exactly one' in result.stderr, result
    result = run(directory, '/imda', '/diskdefs=unused')
    assert result.returncode == 2 and b'only to cpmtools' in result.stderr, result
