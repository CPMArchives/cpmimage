"""Check raw extraction and writes against a separately built POSIX oracle.

Run generate.py first. --tools contains patched cpmcp/mkfs.cpm; --posix
contains POSIX cpmcp. Images and extracted outputs stay in a scratch directory.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('fixtures', type=Path)
parser.add_argument('--tools', required=True, type=Path)
parser.add_argument('--posix', required=True, type=Path)
args = parser.parse_args()
fixtures = args.fixtures.resolve()
tools = args.tools.resolve()
posix = args.posix.resolve()
env = dict(os.environ, CPMTOOLS_DISKDEFS=str(fixtures / 'diskdefs'))


def run(executable, *arguments):
    subprocess.run([str(executable), *map(str, arguments)], env=env, cwd=str(Path(env['CPMTOOLS_DISKDEFS']).parent), check=True)


with tempfile.TemporaryDirectory(prefix='cpmwb-raw-writes-') as tmp:
    directory = Path(tmp)
    for case in json.loads((fixtures / 'cases.json').read_text()):
        image = fixtures / case['image']
        expected = (fixtures / case['expected']).read_bytes()
        for option in ([], ['-T', 'raw']):
            output = directory / 'extracted.bin'
            run(tools / 'cpmcp', *option, '-f', case['format'],
                image, case['filespec'], output)
            assert output.read_bytes() == expected, case['image']
        # Sparse clone on macOS avoids allocating the full 512 MiB test image.
        working = directory / case['image']
        if os.uname().sysname == 'Darwin':
            run('/bin/cp', '-c', image, working)
        else:
            shutil.copyfile(image, working)
        payload = directory / 'replacement.bin'
        payload.write_bytes(bytes(range(255, -1, -1)) * 64)
        oracle = directory / ('posix-' + case['image'])
        if os.uname().sysname == 'Darwin':
            run('/bin/cp', '-c', image, oracle)
        else:
            shutil.copyfile(image, oracle)
        if case['format'] == 'z80pack-hdb':
            # Occupy the earlier free blocks so replacement writes must cross
            # the same logical track boundary as the original failing file.
            for target in (working, oracle):
                with target.open('r+b') as stream:
                    for slot, first in enumerate(range(17, 257, 8), start=2):
                        entry = bytearray(32)
                        entry[0] = 2
                        entry[1:12] = (f'F{slot:07d}' + 'BIN').encode('ascii')
                        entry[12] = 7
                        entry[15] = 128
                        for index in range(8):
                            entry[16+index*2:18+index*2] = (first+index).to_bytes(2, 'little')
                        stream.seek(slot * 32)
                        stream.write(entry)
        for suite, target, option in ((tools, working, ['-T', 'raw']), (posix, oracle, [])):
            run(suite / 'cpmrm', *option, '-f', case['format'], target, case['filespec'])
            run(suite / 'cpmcp', *option, '-f', case['format'],
                target, payload, case['filespec'])
        for executable, option in ((tools / 'cpmcp', ['-T', 'raw']),
                                   (posix / 'cpmcp', [])):
            output = directory / 'roundtrip.bin'
            run(executable, *option, '-f', case['format'],
                working, case['filespec'], output)
            assert output.read_bytes() == payload.read_bytes(), case['image']
        # A POSIX writer must produce exactly the same complete disk image.
        with oracle.open('rb') as before, working.open('rb') as after:
            while True:
                left, right = before.read(1024 * 1024), after.read(1024 * 1024)
                assert left == right, case['image']
                if not left:
                    break
        print(case['image'], 'extraction, write/read, POSIX complete-image comparison passed')

    definitions = directory / 'diskdefs'
    base = (' seclen 512\n tracks 80\n sectrk 9\n blocksize 2048\n'
            ' maxdir 64\n boottrk 0\n skew 0\n os 2.2\n')
    definitions.write_text('diskdef explicit\n' + base + 'end\n'
                           'diskdef named\n' + base + ' libdsk:format ibm360\nend\n')
    env['CPMTOOLS_DISKDEFS'] = str(definitions)
    for fmt, device in [('explicit', 'raw,ibm360'), ('named', 'raw')]:
        image = directory / (fmt + '.raw')
        image.write_bytes(b'\xe5' * 368640)
        run(tools / 'mkfs.cpm', '-f', fmt, image)
        run(tools / 'cpmcp', '-T', device, '-f', fmt, image, payload, '1:PROOF.BIN')
        output = directory / 'explicit.bin'
        run(tools / 'cpmcp', '-T', device, '-f', fmt, image, '1:PROOF.BIN', output)
        assert output.read_bytes() == payload.read_bytes()
        # The explicit LibDsk geometry is compatible with linear POSIX access here.
        run(posix / 'cpmcp', '-f', 'explicit', image, '1:PROOF.BIN', output)
        assert output.read_bytes() == payload.read_bytes()
        print(fmt, device, 'explicit format write/read and POSIX extraction passed')
