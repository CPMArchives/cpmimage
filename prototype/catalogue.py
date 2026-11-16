# SPDX-License-Identifier: GPL-3.0-or-later
"""Index catalogue boundaries. Geometry and filesystem semantics live in cpmtools."""
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import re
import stat
from uuid import uuid4

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


@dataclass(frozen=True)
class ImportDiagnostic:
    line: int
    code: str
    message: str


@dataclass(frozen=True)
class DefinitionImport:
    record: dict
    diagnostics: tuple


def _import_integer(value):
    # strtol(..., 0) accepts legacy octal as well as decimal and hexadecimal.
    if not re.fullmatch(r'\+?(?:0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*)', value):
        raise ValueError('Expected a nonnegative cpmtools integer.')
    digits = value.lstrip('+')
    base = 16 if digits.lower().startswith('0x') else 8 if digits.startswith('0') else 10
    number = int(digits, base)
    if number > 2147483647:
        raise ValueError('Integer exceeds 2147483647.')
    return number


def _import_offset(value, fields):
    match = re.fullmatch(r'(\+?[0-9]+)([KMTS]?)(.*)', value, re.IGNORECASE)
    if not match or (not match[2] and match[3]):
        raise ValueError('Expected a decimal offset with an optional K, M, T or S suffix.')
    count = int(match[1], 10)
    unit = {'': 'bytes', 'K': 'kib', 'M': 'mib', 'T': 'tracks', 'S': 'sectors'}[match[2].upper()]
    if unit in ('tracks', 'sectors') and not {'seclen', 'sectrk', 'tracks'} <= fields.keys():
        raise ValueError('A track or sector offset requires preceding seclen, sectrk and tracks.')
    return {'value': count, 'unit': unit}, bool(match[3]) or value != str(count)


def import_definition(text):
    """Preview a standalone block and retain its source and resolution diagnostics.

    A caller must resolve diagnostics before activating a canonical revision.
    The existing catalogue and FDF loading path continues to use definitions().
    """
    if '\0' in text or len(text.encode('utf-8')) > MAX_CATALOGUE_BYTES:
        raise CatalogueError('Disk definitions must be text no larger than 256 KiB.')
    lines = text.splitlines(keepends=True)
    for number, line in enumerate(lines, 1):
        if len(line.removesuffix('\n').encode('utf-8')) > 254:
            raise CatalogueError(f'Line {number} exceeds the cpmtools line limit.')
    blocks = definitions(text)
    if len(blocks) != 1:
        raise CatalogueError('Import requires exactly one standalone definition.')
    identifier = next(iter(blocks))
    fields = dict(dirblks=None, skew={'mode': 'default'}, os=None,
                  offset={'value': 0, 'unit': 'bytes'}, logicalextents=None, libdsk_format=None)
    integer_keys = {'seclen', 'sectrk', 'tracks', 'blocksize', 'maxdir',
                    'dirblks', 'logicalextents', 'boottrk', 'bootsec', 'skew'}
    seen = set()
    diagnostics = []
    boot = {}
    skew_table = None
    begin = end = 0

    def report(line, code, message):
        diagnostics.append(ImportDiagnostic(line, code, message))

    for number, line in enumerate(lines, 1):
        syntax = re.split(r'[;#]', line, maxsplit=1)[0].strip().split(None, 1)
        if not syntax:
            continue
        key = syntax[0]
        if key == 'diskdef':
            begin = number - 1
            continue
        if key == 'end' and len(syntax) == 1:
            end = number
            break
        key = 'os' if key == 'OS' else key
        if key in seen:
            report(number, 'multiple_os' if key == 'os' else 'duplicate_directive',
                   f'Repeated {key} directive requires review.')
        other = {'bootsec': 'boottrk', 'boottrk': 'bootsec',
                 'skew': 'skewtab', 'skewtab': 'skew'}.get(key)
        if other in seen:
            report(number, 'redundant_directive', f'Both {key} and {other} are present.')
        seen.add(key)
        value = syntax[1] if len(syntax) == 2 else ''
        try:
            if key in integer_keys:
                parsed = _import_integer(value)
                if key in ('bootsec', 'boottrk'):
                    boot[key] = parsed
                elif key == 'skew':
                    fields['skew'] = {'mode': 'step', 'step': parsed}
                else:
                    fields[key] = (parsed or None) if key in ('dirblks', 'logicalextents') else parsed
            elif key == 'skewtab':
                # The native parser treats table items as decimal, even with leading zeroes.
                if not re.fullmatch(r'\+?[0-9]+(?:(?:,\s*|\s+|(?=\+))\+?[0-9]+)*,?', value):
                    raise ValueError('Expected decimal sector slots.')
                skew_table = {'mode': 'table', 'slots': [int(slot) for slot in re.findall(r'[0-9]+', value)]}
            elif key == 'offset':
                fields[key], noncanonical = _import_offset(value, fields)
                if noncanonical:
                    report(number, 'noncanonical_offset', 'Offset will be rendered as decimal bytes.')
            elif key == 'os':
                if value not in ('2.2', '3', 'isx', 'p2dos', 'zsys'):
                    raise ValueError('Unknown OS value.')
                fields[key] = value
            elif key == 'libdsk:format':
                if not value:
                    raise ValueError('Expected a LibDsk geometry name.')
                fields['libdsk_format'] = value
            else:
                report(number, 'unknown_directive', f'Unknown directive {key}.')
        except ValueError as error:
            report(number, 'invalid_value', f'{key}: {error}')

    if boot:
        key = 'bootsec' if 'bootsec' in boot else 'boottrk'
        fields['boot'] = {'unit': 'sectors' if key == 'bootsec' else 'tracks', 'count': boot[key]}
    if skew_table is not None:
        fields['skew'] = skew_table
    for key in ('seclen', 'tracks', 'sectrk', 'blocksize', 'maxdir', 'boot'):
        if key not in fields:
            report(begin + 1, 'missing_directive', f'Missing required {key} value.')

    record = {'schema_version': 1, 'record_id': str(uuid4()), 'identifier': identifier,
              'definition': fields, 'import_source': {'diskdefs_text': ''.join(lines[begin:end])}}
    return DefinitionImport(record, tuple(diagnostics))
