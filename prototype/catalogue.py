# SPDX-License-Identifier: GPL-3.0-or-later
"""Index catalogue boundaries. Geometry and filesystem semantics live in cpmtools."""
import hashlib
import os
from pathlib import Path
import re
import stat

MAX_CATALOGUE_BYTES = 256 * 1024
IDENTIFIER = re.compile(r'[^\s\x00-\x1f\x7f#;]{1,160}')


class CatalogueError(ValueError):
    pass


def read_text(path):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0))
    with os.fdopen(descriptor, 'rb') as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise CatalogueError('Choose a regular disk-definition file.')
        data = stream.read(MAX_CATALOGUE_BYTES + 1)
    if len(data) > MAX_CATALOGUE_BYTES or b'\0' in data:
        raise CatalogueError('Disk definitions must be text no larger than 256 KiB.')
    try:
        return data.decode('utf-8-sig')
    except UnicodeError as error:
        raise CatalogueError('Disk definitions must be UTF-8 text.') from error


def definitions(text, bundled=False):
    blocks = {}
    name = None
    lines = []
    for number, line in enumerate(text.splitlines(), 1):
        syntax = re.split(r'[;#]', line, maxsplit=1)[0].split()
        if not syntax:
            if name:
                lines.append(line)
            continue
        if len(line.encode('utf-8')) >= 255:
            raise CatalogueError(f'Line {number} exceeds the cpmtools line limit.')
        if syntax[0] == 'diskdef':
            if name:
                if not bundled:
                    raise CatalogueError(f'Missing end for {name} before line {number}.')
                blocks[name] = '\n'.join([*lines, 'end']) + '\n'
            if len(syntax) != 2 or not IDENTIFIER.fullmatch(syntax[1]) or not syntax[1].isascii():
                raise CatalogueError(f'Invalid diskdef identifier on line {number}.')
            name = syntax[1]
            if name in blocks or name == 'amstrad':
                raise CatalogueError(f'Duplicate or reserved identifier {name}.')
            lines = [line]
        elif name and syntax == ['end']:
            blocks[name] = '\n'.join([*lines, line]) + '\n'
            name = None
        elif name:
            lines.append(line)
        else:
            raise CatalogueError(f'Expected a diskdef on line {number}.')
    if name:
        raise CatalogueError(f'Missing end for {name}.')
    if not blocks:
        raise CatalogueError('The catalogue contains no disk definitions.')
    return blocks


def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def sidecar(image):
    candidates = []
    for entry in image.parent.iterdir():
        if entry.suffix.casefold() == '.fdf' and entry.stem == image.stem:
            candidates.append(entry)
            if len(candidates) > 1:
                raise CatalogueError('Multiple matching FDF sidecars.')
    return candidates[0] if candidates else None
