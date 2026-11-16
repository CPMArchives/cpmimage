# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate and render authored definitions before passing them to cpmtools."""
from dataclasses import dataclass
from functools import lru_cache
import json
from pathlib import Path
import subprocess
import tempfile

from jsonschema import Draft202012Validator, FormatChecker, ValidationError

from catalogue import digest


class DiskFormatError(ValueError):
    pass


@lru_cache(maxsize=2)
def _validator(name):
    path = Path(__file__).with_name('schemas') / (name + '.schema.json')
    schema = json.loads(path.read_text(encoding='utf-8'))
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema, format_checker=FormatChecker())


def _validate(name, value):
    try:
        _validator(name).validate(value)
    except ValidationError as error:
        location = '.'.join(map(str, error.absolute_path)) or '<root>'
        raise DiskFormatError(f'{location}: {error.message}') from error


def validate_mapping_sidecar(profile):
    """Check new sidecar output. Legacy opens use the existing mapping verifier."""
    _validate('mapping-sidecar', profile)


@dataclass(frozen=True)
class RenderedDefinition:
    identifier: str
    text: str

    @property
    def revision(self):
        return digest(self.text)

    def describe(self, helper, timeout=10):
        helper = Path(helper).resolve()
        with tempfile.TemporaryDirectory(prefix='cpmimage-definition-') as directory:
            (Path(directory) / 'diskdefs').write_bytes(self.text.encode('utf-8'))
            try:
                result = subprocess.run([str(helper), 'describe', self.identifier],
                                        cwd=directory, capture_output=True, timeout=timeout)
            except (OSError, subprocess.TimeoutExpired) as error:
                raise DiskFormatError(f'Cannot run disk definition helper: {error}') from error
        if result.returncode:
            detail = result.stderr or result.stdout
            raise DiskFormatError(detail.decode('utf-8', errors='replace').strip()
                                  or f'Disk definition helper exited with status {result.returncode}.')
        try:
            response = json.loads(result.stdout)
        except (ValueError, UnicodeError) as error:
            raise DiskFormatError('Invalid disk definition helper response.') from error
        if not isinstance(response, dict) or response.get('v') != 1 or response.get('ok') is not True:
            raise DiskFormatError('Invalid disk definition helper response.')
        return response


def render_definition(record):
    """Render schema-valid authored data. Call describe for native validation."""
    _validate('disk-format-definition', record)
    identifier = record['identifier']
    fields = record['definition']
    lines = [f'diskdef {identifier}']

    def emit(key, value):
        if isinstance(value, (int, float)):
            value = int(value)
        lines.append(f'  {key} {value}')

    for key in ('seclen', 'tracks', 'sectrk', 'blocksize', 'maxdir', 'dirblks'):
        if fields[key] is not None:
            emit(key, fields[key])
    boot = fields['boot']
    emit('boottrk' if boot['unit'] == 'tracks' else 'bootsec', boot['count'])
    skew = fields['skew']
    if skew['mode'] == 'step':
        emit('skew', skew['step'])
    elif skew['mode'] == 'table':
        emit('skewtab', ','.join(str(int(slot)) for slot in skew['slots']))
    if fields['os'] is not None:
        emit('os', fields['os'])

    offset = fields['offset']
    multiplier = {'bytes': 1, 'kib': 1024, 'mib': 1048576,
                  'sectors': int(fields['seclen']),
                  'tracks': int(fields['sectrk']) * int(fields['seclen'])}[offset['unit']]
    effective_offset = int(offset['value']) * multiplier
    if effective_offset > 2147483647:
        raise DiskFormatError('definition.offset: effective byte offset exceeds 2147483647.')
    if effective_offset:
        emit('offset', effective_offset)
    for key, directive in (('logicalextents', 'logicalextents'), ('libdsk_format', 'libdsk:format')):
        if fields[key] is not None:
            emit(directive, fields[key])
    lines.append('end')
    for number, line in enumerate(lines, 1):
        if len(line.encode('utf-8')) > 254:
            raise DiskFormatError(f'Rendered line {number} exceeds the cpmtools line limit.')
    return RenderedDefinition(identifier, '\n'.join(lines) + '\n')
