# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage and journal a save beside its destination before replacing anything."""
import hashlib
import json
import os
from pathlib import Path
import shutil


class PublicationError(OSError):
    pass


def fingerprint(path):
    try:
        with Path(path).open('rb') as stream:
            digest = hashlib.sha256()
            for chunk in iter(lambda: stream.read(65536), b''):
                digest.update(chunk)
            return digest.hexdigest()
    except FileNotFoundError:
        return None


def sync_directory(path):
    if os.name != 'nt':
        descriptor = os.open(path, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def stage_copy(source, destination):
    with Path(source).open('rb') as reader, Path(destination).open('xb') as writer:
        shutil.copyfileobj(reader, writer)
        writer.flush()
        os.fsync(writer.fileno())


def recovery_directory(destination):
    return destination.parent / ('.' + destination.name + '.cpmimage-recovery')


def publish(owner, expected, image=None, sidecar=None, backups=True):
    """Publish previously validated files under an acquired destination lock.

    sidecar is (target, expected digest, validated staged source). A durable
    recovery directory retains the previous bytes and a manifest on failure.
    """
    destination = owner.path
    recovery = recovery_directory(destination)
    owner.check(replacing=image is not None)
    if fingerprint(destination) != expected:
        raise PublicationError('The destination contents changed.')
    changes = []
    if image is not None:
        changes.append((destination, expected, image))
    if sidecar is not None:
        changes.append(sidecar)
    if not changes:
        return
    try:
        recovery.mkdir(mode=0o700)
    except FileExistsError as error:
        raise PublicationError(f'Review the previous interrupted save in {recovery}.') from error
    journal_ready = False
    try:
        records = []
        for index, (target, previous, source) in enumerate(changes):
            target = Path(target)
            if target.parent != destination.parent or target.is_symlink():
                raise PublicationError('Save targets must be regular sibling paths.')
            if fingerprint(target) != previous:
                raise PublicationError(f'The destination changed: {target.name}')
            stage_copy(source, recovery / f'new-{index}')
            if previous is not None:
                stage_copy(target, recovery / f'old-{index}')
                if fingerprint(recovery / f'old-{index}') != previous:
                    raise PublicationError(f'The destination changed while staging: {target.name}')
                if backups:
                    backup = target.with_name(target.name + '.BAK')
                    if backup.is_symlink() or (backup.exists() and not backup.is_file()):
                        raise PublicationError(f'Cannot replace backup: {backup.name}')
                    if backup.exists():
                        stage_copy(backup, recovery / f'previous-backup-{index}')
                    stage_copy(recovery / f'old-{index}', recovery / f'backup-{index}')
            records.append(dict(target=target.name, previous=previous,
                                replacement=fingerprint(recovery / f'new-{index}')))
        if image is not None:
            owner.reserve_replacement(recovery / 'new-0')
        with (recovery / 'manifest.json').open('x', encoding='utf-8') as stream:
            json.dump(dict(version=1, backups=backups, files=records), stream, indent=2)
            stream.flush()
            os.fsync(stream.fileno())
        sync_directory(recovery)
        sync_directory(destination.parent)
        journal_ready = True
        owner.check(replacing=image is not None)
        for target, record in zip((Path(change[0]) for change in changes), records):
            if fingerprint(target) != record['previous'] or target.is_symlink():
                raise PublicationError(f'The destination changed: {target.name}')
        for index, (target, previous, _) in enumerate(changes):
            target = Path(target)
            if backups and previous is not None:
                os.replace(recovery / f'backup-{index}', target.with_name(target.name + '.BAK'))
                sync_directory(destination.parent)
            if fingerprint(target) != previous:
                raise PublicationError(f'The destination changed: {target.name}')
            os.replace(recovery / f'new-{index}', target)
            if index == 0 and image is not None:
                owner.adopt_replacement()
            sync_directory(destination.parent)
    except Exception as error:
        if journal_ready:
            raise PublicationError(f'Save interrupted. Recovery data is in {recovery}. {error}') from error
        shutil.rmtree(recovery)
        owner.cancel_replacement()
        raise
    shutil.rmtree(recovery)
    sync_directory(destination.parent)
