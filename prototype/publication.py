# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage and journal a save beside its destination before replacing anything."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import uuid


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


def require_recovery_clear(destination, targets):
    journal = recovery_directory(destination)
    if journal.exists() or journal.is_symlink():
        raise PublicationError(f'Review the previous interrupted save in {journal}.')
    names = {Path(target).name.casefold() for target in targets}
    for other in destination.parent.glob('.*.cpmimage-recovery'):
        manifest = other / 'manifest.json'
        if not manifest.is_file():
            continue  # No live replacements occur before a manifest is durable.
        try:
            records = json.loads(manifest.read_text(encoding='utf-8'))['files']
            affected = {record['target'].casefold() for record in records}
            affected.update(name + '.bak' for name in tuple(affected))
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            raise PublicationError(f'Cannot verify save recovery state in {other}.') from error
        if names & affected:
            raise PublicationError(f'Review the previous interrupted save in {other}.')


def record_commit(recovery):
    staged = recovery / 'commit.pending'
    with staged.open('xb') as marker:
        marker.write(b'committed\n')
        marker.flush()
        os.fsync(marker.fileno())
    os.replace(staged, recovery / 'committed')
    sync_directory(recovery)


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
    require_recovery_clear(destination, [destination, *(item[0] for item in changes),
        *(Path(item[0]).with_name(Path(item[0]).name + ".BAK") for item in changes)])
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
            prior_backup = recovery / f"previous-backup-{index}"
            records.append(dict(target=target.name, previous=previous,
                                previous_backup=fingerprint(prior_backup),
                                replacement=fingerprint(recovery / f'new-{index}')))
        if image is not None:
            owner.reserve_replacement(recovery / 'new-0')
        with (recovery / 'manifest.json').open('x', encoding='utf-8') as stream:
            json.dump(dict(version=2, backups=backups, files=records), stream, indent=2)
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
            if fingerprint(target) != previous:
                raise PublicationError(f'The destination changed: {target.name}')
            os.replace(recovery / f'new-{index}', target)
            if index == 0 and image is not None:
                owner.adopt_replacement()
            sync_directory(destination.parent)
        # Progress only. Save commits after every required backup is durable.
        with (recovery / 'outputs-published').open('xb') as marker:
            os.fsync(marker.fileno())
        sync_directory(recovery)
        for index, (target, previous, _) in enumerate(changes):
            if backups and previous is not None:
                target = Path(target)
                backup = target.with_name(target.name + '.BAK')
                os.replace(recovery / f'backup-{index}', backup)
                sync_directory(destination.parent)
        record_commit(recovery)
    except Exception as error:
        if journal_ready:
            raise PublicationError(f'Save interrupted or commit uncertain. Recovery data is in {recovery}. {error}') from error
        shutil.rmtree(recovery)
        owner.cancel_replacement()
        raise
    # Cleanup is after publication and backup rotation. Its failure must not
    # retroactively turn a completed save into a failed save.
    try:
        cleanup = recovery.with_name(f'.cpmimage-cleanup-{uuid.uuid4().hex}')
        os.replace(recovery, cleanup)
        sync_directory(destination.parent)
        shutil.rmtree(cleanup)
    except OSError:
        pass
