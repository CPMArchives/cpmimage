# SPDX-License-Identifier: GPL-3.0-or-later
"""User-authored disk definitions stored as individually replaceable JSON records."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
from uuid import UUID

from source_ownership import native_lock


class DefinitionStoreError(ValueError):
    pass


def atomic_write(path, data):
    """Stage in the destination directory so readers see either complete version."""
    path = Path(path)
    descriptor, staged = tempfile.mkstemp(prefix='.cpmimage-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(staged, path)
    finally:
        Path(staged).unlink(missing_ok=True)


class DefinitionStore:
    def __init__(self, directory):
        self.directory = Path(directory)

    def records(self):
        records = []
        unsupported = []
        for path in sorted(self.directory.glob('*.json')):
            try:
                if path.stat().st_size > 2 * 1024 * 1024:
                    raise ValueError('Definition record exceeds the storage limit.')
                record = json.loads(path.read_bytes())
                if not isinstance(record, dict):
                    raise ValueError('Expected a definition record object.')
                version = record.get('schema_version')
                if type(version) is int and version != 1:
                    unsupported.append(path.name)
                    continue
                if str(UUID(record['record_id'])) != path.stem:
                    raise ValueError('Record ID does not match its filename.')
            except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
                raise DefinitionStoreError(f'{path.name}: {error}') from error
            records.append(record)
        return records, unsupported

    def save(self, record, reserved_identifiers, validate_records, expected_record_sha256=None):
        self.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        record_id = str(UUID(record['record_id']))
        path = self.directory / (record_id + '.json')
        with native_lock(self.directory / '.lock'):
            records, unsupported = self.records()
            if path.name in unsupported:
                raise DefinitionStoreError('This record uses an unsupported schema version.')
            if expected_record_sha256 is not None:
                current = hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else ''
                if current != expected_record_sha256:
                    raise DefinitionStoreError('The saved definition changed. Load it again.')
            others = [other for other in records if str(UUID(other['record_id'])) != record_id]
            if any(other.get('identifier') == record['identifier'] for other in others):
                raise DefinitionStoreError(f"Duplicate format identifier {record['identifier']}.")
            if record['identifier'] in reserved_identifiers:
                raise DefinitionStoreError(f"Format identifier {record['identifier']} belongs to a catalogue.")
            validate_records([*others, record])
            data = encode_record(record)
            if len(data) > 2 * 1024 * 1024:
                raise DefinitionStoreError('Definition record exceeds the storage limit.')
            atomic_write(path, data)
        return [*others, record]


def encode_record(record):
    return (json.dumps(record, ensure_ascii=False, sort_keys=True, indent=2) + '\n').encode('utf-8')
