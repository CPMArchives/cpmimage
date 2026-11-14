# SPDX-License-Identifier: GPL-3.0-or-later
"""Append an already assembled Windows package to a native console launcher."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tarfile
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('launcher', type=Path)
parser.add_argument('package', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    payload = Path(directory) / 'payload.tar.gz'
    with tarfile.open(payload, 'w:gz') as archive:
        for child in sorted(args.package.iterdir()):
            archive.add(child, arcname=child.name)
    with payload.open('rb') as stream:
        hasher = hashlib.sha256()
        for chunk in iter(lambda: stream.read(65536), b''):
            hasher.update(chunk)
        digest = hasher.hexdigest()
    with args.output.open('xb') as out:
        for path in (args.launcher, payload):
            with path.open('rb') as stream:
                shutil.copyfileobj(stream, out)
        out.write(f'{digest}{payload.stat().st_size:016d}'.encode('ascii'))
