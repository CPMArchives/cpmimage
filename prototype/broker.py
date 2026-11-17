# SPDX-License-Identifier: GPL-3.0-or-later
"""Local review prototype: retained snapshots, read-only helper, bounded JSON lines.

Reuses the qualified experimental parent, not a new filesystem implementation.
Only one operation runs at a time; cancellation is cooperative through the parent.
"""
import argparse
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import signal
import shutil
import stat
import sys
import tempfile
import threading
import uuid



MAX_IMAGE_BYTES = 128 * 1024 * 1024
EDITOR_OPERATIONS = frozenset({
    'definition_list', 'definition_import', 'definition_validate', 'definition_check_image',
    'definition_save', 'definition_export', 'mapping_preview', 'mapping_write',
})
MAX_EDITOR_REQUEST_BYTES = 2 * 1024 * 1024
MAX_INSPECT_REPLY_BYTES = 256 * 1024
DSK_MAGIC = b'MV - CPCEMU Disk-File\r\nDisk-Info\r\n'


def check_standard_dsk(data, geometry, backend, *, observations=None):
    # The existing container scope is the qualified uniform, single-sided
    # 40-track map. Match properties of the definition, not its identifier.
    if (geometry.get('sector_size'), geometry.get('tracks'),
            geometry.get('sectors_per_track'), geometry.get('offset')) != (512, 40, 10, 0):
        raise backend.Failure('unsupported_container', 'Standard DSK support requires a 40-track, single-sided 512 × 10 sector map.')
    if geometry['libdsk_format'] not in ('', 'ampro200', 'pcw200'):
        raise backend.Failure('unsupported_container', 'The LibDsk geometry does not match the supported standard DSK map.')
    track_size = 256 + 512 * 10
    if (len(data) != 256 + 40 * track_size or data[48:50] != bytes([40, 1])
            or int.from_bytes(data[50:52], 'little') != track_size):
        raise backend.Failure('image_incompatible', 'Standard DSK geometry or track size differs from the selected definition.')
    for track in range(40):
        start = 256 + track * track_size
        header = data[start:start + 256]
        if (header[:12] != b'Track-Info\r\n' or header[16:18] != bytes([track, 0])
                or header[20:22] != bytes([2, 10])):
            raise backend.Failure('image_incompatible', 'Invalid standard DSK track header.')
        for index in range(10):
            sector = 24 + index * 8
            if header[sector:sector + 6] != bytes([track, 0, index + 1, 2, 0, 0]):
                raise backend.Failure('unsupported_container', 'The DSK contains a different sector order or sector error flags.')
        if observations is not None:
            observations[track, 0] = tuple((header[26 + index * 8], 512) for index in range(10))


def raw_extent(data, geometry, backend):
    if geometry.get('boot_geometry'):
        return len(data)
    sector = geometry['sector_size']
    offset = geometry['offset']
    end = offset + sector * geometry['tracks'] * geometry['sectors_per_track']
    if end > MAX_IMAGE_BYTES:
        raise backend.Failure('unsupported_backend', 'The definition exceeds the 128 MiB working image limit.')
    if len(data) <= offset or (len(data) - offset) % sector or (not offset and len(data) > end):
        raise backend.Failure('image_size', 'The RAW image size is incompatible with the selected geometry.')
    return end


class SourceTracks(dict):
    """Source track addresses and sector descriptors collected during staging."""

    def __init__(self):
        super().__init__()
        self.imd_comment = b''
        self.modes = {}
        self.densities = {}
        self.statuses = {}


def classify_comparison(request, backend):
    """Classify bounded batches of pairs joined by file identity or directory slot."""
    view, pairs = request.get('view'), request.get('pairs')
    if view not in ('files', 'directory') or not isinstance(pairs, list) or len(pairs) > 64:
        raise backend.Failure('invalid_request', 'Choose a comparison view and up to 64 pairs.')

    def digest(value):
        return (isinstance(value, str) and len(value) == 64
                and all(char in '0123456789abcdef' for char in value))

    definitions = request.get('left_definition'), request.get('right_definition')
    if not all(digest(value) for value in definitions):
        raise backend.Failure('invalid_request', 'Send both disk definition fingerprints.')
    same_definition = definitions[0] == definitions[1]

    def checked_extents(rows):
        if rows is None:
            return None
        fields = ('slot', 'extent', 'physical_extent', 'record_count')
        if not isinstance(rows, list) or any(
                not isinstance(row, dict)
                or any(type(row.get(field)) is not int or row[field] < 0 for field in fields)
                or not isinstance(row.get('pointers'), list) or len(row['pointers']) > 16
                or any(block is not None and (type(block) is not int or not 1 <= block <= 65535)
                       for block in row['pointers']) for row in rows):
            raise backend.Failure('invalid_request', 'Provide directory extents with ordered block pointers.')
        return rows

    def structure_changes(left, right):
        if left is None or right is None:
            return None

        def coverage(rows):
            return sorted((row['extent'], row['physical_extent'], row['record_count']) for row in rows)

        def allocation(rows):
            return sorted((row['physical_extent'], tuple(-1 if block is None else block
                          for block in row['pointers'])) for row in rows)

        # Ignore the ordinal in the extent view. Keep repeated records visible.
        return dict(extents_changed=coverage(left) != coverage(right),
                    slots_changed=sorted(row['slot'] for row in left) != sorted(row['slot'] for row in right),
                    allocation_changed=allocation(left) != allocation(right) if same_definition else None)

    def directory_signature(row):
        fields = ('kind', 'status', 'user', 'name', 'extension', 'extent', 'physical_extent',
                  'record_count', 'attributes')
        return tuple(row.get(field) for field in fields)

    results = []
    seen = set()
    for pair in pairs:
        if not isinstance(pair, dict):
            raise backend.Failure('invalid_request', 'Each comparison pair must be an object.')
        left, right = pair.get('left'), pair.get('right')
        sides = [side for side in (left, right) if side is not None]
        if not sides or not all(isinstance(side, dict) for side in sides):
            raise backend.Failure('invalid_request', 'Each pair must contain at least one comparison row.')
        if view == 'files':
            if not all(isinstance(side.get('name'), str) for side in sides):
                raise backend.Failure('invalid_request', 'File rows require a CP/M name.')
            keys = [backend.filename(side['name'], True) for side in sides]
            if any(type(side.get('size')) is not int or not 0 <= side['size'] <= MAX_IMAGE_BYTES
                   or type(side.get('attr')) is not int or not 0 <= side['attr'] <= 16383 for side in sides):
                raise backend.Failure('invalid_request', 'File rows require a size and CP/M attributes.')
        else:
            keys = [side.get('slot') for side in sides]
            if any(type(slot) is not int or not 0 <= slot <= 0x7fffffff for slot in keys):
                raise backend.Failure('invalid_request', 'Directory rows require a valid slot.')
        key = keys[0]
        if key in seen or any(item != key for item in keys):
            raise backend.Failure('invalid_request', 'Comparison identities must match and appear only once per batch.')
        seen.add(key)
        presence = 'removed' if right is None else 'added' if left is None else 'both'
        if view == 'directory':
            fields_changed = directory_signature(left) != directory_signature(right) if presence == 'both' else None
            has_pointers = presence == 'both' and (left.get('kind') == 'file' or right.get('kind') == 'file')
            allocation_changed = (left.get('pointers') != right.get('pointers')
                                  if has_pointers and same_definition else None)
            status = presence
            if presence == 'both':
                if fields_changed or allocation_changed:
                    status = 'changed'
                elif has_pointers and not same_definition:
                    status = 'unavailable'
                else:
                    status = 'identical'
            results.append(dict(slot=key, status=status, fields_changed=fields_changed,
                                allocation_changed=allocation_changed))
            continue

        result = dict(name=key, user=int(key[:2]), filename=key[2:], status=presence,
                      content_changed=None, attributes_changed=None, structure=None)
        if presence == 'both':
            attributes = left['attr'] != right['attr']
            content = None
            if left['size'] != right['size']:
                content = True
            elif digest(left.get('sha256')) and digest(right.get('sha256')):
                content = left['sha256'] != right['sha256']
            result.update(content_changed=content, attributes_changed=attributes,
                          structure=structure_changes(checked_extents(left.get('extents')),
                                                      checked_extents(right.get('extents'))),
                          status='unavailable' if content is None else
                                 'changed' if content or attributes else 'identical')
        results.append(result)
    key = 'name' if view == 'files' else 'slot'
    return dict(view=view, same_definition=same_definition, rows=sorted(results, key=lambda row: row[key]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--helper', type=Path, required=True)
    parser.add_argument('--definition-store', type=Path,
                        default=Path.home() / '.cpmimage' / 'disk-formats')
    args = parser.parse_args()
    sys.path.insert(0, str(args.root / 'experiments/process-comparison'))
    sys.path.insert(0, str(args.root / 'prototype'))
    import catalogue
    from container_mapping import MappingError, MappingProfileRequired, PhysicalMapping, inspect_imd, transfer_dmk, dmk_template, replace_imd_comment, verify_jv3, derive_mapping, strict_profile
    from publication import fingerprint as file_hash, publish, recovery_directory, require_recovery_clear
    from source_ownership import Ownership, OwnershipError, identity, native_lock
    from disk_format_store import DefinitionStore, DefinitionStoreError, atomic_write, encode_record
    store = DefinitionStore(args.definition_store)
    import adapters as backend
    backend.HELPER = args.helper.resolve()
    bundled_path = args.root / 'fixtures/diskdefs'
    definitions = catalogue.definitions(catalogue.read_text(bundled_path), bundled=True)
    definitions['amstrad'] = ''
    bundled_identifiers = frozenset(definitions)
    base_definitions = definitions.copy()
    saved_loaded = False
    metadata = json.loads((args.root / 'fixtures/diskdefs-metadata.json').read_text(encoding='utf-8'))
    descriptions = {}
    # Only internal command construction can select this guard; never a request.
    def guard(argv):
        if os.name == 'nt':
            return argv
        return [sys.executable, '-I', '-B', str(args.root / 'prototype/helper_guard.py'), str(os.getpid()), *argv]

    def adapter():
        return backend.Adapter('helper', timeout=10, cancel=stop, fault=guard, read_only=True)

    stop = threading.Event()
    output_lock = threading.Lock()
    snapshot = None
    session = None
    worker = None
    active_id = None
    completed = threading.Event()

    def emit(value):
        with output_lock:
            print(json.dumps(value), flush=True)

    def respond(value):
        # Publish completion before the reply; the client may immediately send again.
        completed.set()
        emit(value)

    def read_image(path):
        fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0))
        with os.fdopen(fd, 'rb') as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_IMAGE_BYTES:
                raise backend.Failure('input_limit', 'Choose a regular disk-image file no larger than 128 MiB.')
            data = stream.read(MAX_IMAGE_BYTES + 1)
        if len(data) > MAX_IMAGE_BYTES:
            raise backend.Failure('input_limit', 'Image exceeds the 128 MiB working image limit.')
        return data

    def working_revision(request):
        if not session or request.get('session') != session['token']:
            raise backend.Failure('stale_session', 'Open an image first.')
        # Working-image edits publish by replacement. Include the mount and file
        # identity so a page cannot be resumed after an edit, format switch or Discard.
        info = session['image'].stat()
        definition_hash = catalogue.digest(session['definitions'])
        revision = hashlib.sha256(repr((
            session['token'], definition_hash, info.st_dev, info.st_ino, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns, session['dirty'],
        )).encode('ascii')).hexdigest()
        if request.get('revision', revision) != revision:
            raise backend.Failure('stale_revision', 'The working image changed. Reload the results.')
        return dict(revision=revision, format=session['format'],
                    definition_sha256=definition_hash, dirty=session['dirty'])

    def comparison_files(context, rid):
        cached = session.get('comparison_files')
        if cached is not None and cached[0] == context['revision']:
            return cached[1]
        reply = adapter().request(dict(v=1, id=rid, op='list', image=str(session['image']),
                                       format=session['format'], container='raw'),
                                  defs_override=session['definitions'])
        if not reply['ok']:
            raise backend.Failure(reply['error'], reply.get('message', ''))
        listing = reply['result']
        result = dict(files={row['name']: {key: row[key] for key in ('name', 'size', 'attr')}
                             for row in listing['entries']},
                      free_bytes=listing.get('free_bytes'), total_bytes=listing.get('total_bytes'))
        session['comparison_files'] = context['revision'], result
        return result

    def install_system_tracks(image, geometry, path, pad):
        start = geometry.get('offset', 0)
        size = geometry.get('boot_sectors', 0) * geometry.get('sector_size', 0)
        if not size or start + size > image.stat().st_size:
            raise backend.Failure('unsupported_backend', 'No accessible reserved system-track region.')
        with Path(path).open('rb') as stream:
            data = stream.read(size + 1)
        if len(data) > size:
            raise backend.Failure('input_limit', 'System-track input exceeds the reserved region.')
        if len(data) < size and not pad:
            raise backend.Failure('padding_required', 'Confirm E5 padding of the short system-track input.')
        with image.open('r+b') as stream:
            stream.seek(start)
            stream.write(data.ljust(size, b'\xe5'))

    def describe(fmt, definition, directory):
        key = (fmt, catalogue.digest(definition))
        if key not in descriptions:
            (directory / 'diskdefs').write_text(definition, encoding='utf-8')
            descriptions[key] = adapter().native(directory, 'describe', fmt)
        return descriptions[key]

    def validate_catalogue(blocks):
        rows = []
        with tempfile.TemporaryDirectory(prefix='cpmimage-catalogue-') as temp:
            directory = Path(temp)
            (directory / 'diskdefs').write_text(''.join(blocks.values()), encoding='utf-8')
            identifiers = list(blocks)
            for start in range(0, len(identifiers), 32):
                chunk = identifiers[start:start + 32]
                parsed = adapter().native(directory, 'describe-catalogue', *chunk)['definitions']
                if not isinstance(parsed, list) or len(parsed) != len(chunk):
                    raise backend.Failure("protocol_invalid", "Incomplete disk definition response.")
                for fmt, geometry in zip(chunk, parsed):
                    digest = catalogue.digest(blocks[fmt])
                    descriptions[fmt, digest] = geometry
                    manufacturer, model, family = metadata.get(fmt, ('', '', ''))
                    rows.append(dict(id=fmt, manufacturer=manufacturer, model=model, family=family,
                                     definition_sha256=digest, geometry=geometry))
        return sorted(rows, key=lambda row: (row['id'].casefold(), row['id']))

    def resolve_sidecar(source, directory):
        path = None
        try:
            path = catalogue.sidecar(source)
            if path is None:
                return None
            blocks = catalogue.definitions(catalogue.read_text(path))
            if len(blocks) != 1:
                raise catalogue.CatalogueError('An FDF must contain exactly one standalone definition.')
            fmt, definition = next(iter(blocks.items()))
            geometry = describe(fmt, definition, directory)
        except (OSError, catalogue.CatalogueError, backend.Failure) as error:
            if isinstance(error, backend.Failure) and error.code == 'cancelled':
                raise
            raise backend.Failure('invalid_sidecar', f'{path.name if path else source.name}: {error}') from error
        return fmt, definition, geometry

    structured = ('imd', 'jv3', 'dmk')

    def image_container(source, data, requested=None):
        if source.suffix.lower() == '.td0':
            return 'td0'
        if requested:
            return requested
        if data.startswith(DSK_MAGIC):
            return 'dsk'
        if data.startswith(b'IMD '):
            return 'imd'
        extension = source.suffix.lower().lstrip('.')
        return extension if extension in structured else 'raw'

    def mapped_image(container, source, data, geometry, directory, raw, *, proposed_profile=None, observations=None, mapping_details=None):
        profile = source.with_name(source.name + '.mapping.json')
        try:
            if proposed_profile is not None:
                mapping = PhysicalMapping.from_profile(proposed_profile, geometry)
                mapping_source = 'proposed'
            else:
                try:
                    mapping = PhysicalMapping.from_profile(json.loads(catalogue.read_text(profile)), geometry)
                    mapping_source = 'sidecar'
                except FileNotFoundError:
                    mapping = derive_mapping(container, data, geometry)
                    mapping_source = 'inferred'
            if mapping_details is not None:
                mapping_details['mapping_source'] = mapping_source
            if container == 'dmk':
                def observe(cylinder, head, sectors):
                    observations[cylinder, head] = sectors
                raw.write_bytes(transfer_dmk(data, mapping, observe=observe if observations is not None else None))
                return mapping
            if container == 'imd':
                inventory, comment = inspect_imd(
                    data, statuses=observations.statuses if observations is not None else None)
                mapping.verify_inventory(inventory)
                if observations is not None:
                    for key, track in inventory.items():
                        observations[key] = tuple((sector, track.size) for sector in track.sectors)
                    observations.imd_comment = comment
                    observations.modes = {key: track.mode for key, track in inventory.items()}
            if container == 'jv3':
                inventory = verify_jv3(data, mapping)
                if observations is not None:
                    tracks = {}
                    for (cylinder, head, sector), (size, mfm) in inventory.items():
                        tracks.setdefault((cylinder, head), []).append((sector, size))
                        observations.densities[cylinder, head, sector] = 'mfm' if mfm else 'fm'
                    observations.update((key, tuple(sectors)) for key, sectors in tracks.items())
            transfer(container, mapping, directory, raw, data, False)
            return mapping
        except (MappingError, OSError, ValueError) as error:
            raise backend.Failure('physical_mapping', f'{profile.name}: {error}') from error

    def require_separate_archive(session, destination):
        archive = session.get('archive_source')
        if archive and (destination.resolve() == archive.resolve()
                        or (destination.exists() and identity(destination.stat()) == session['archive_identity'])):
            raise backend.Failure('invalid_destination', 'Choose a destination separate from the original TeleDisk archive.')

    def teledisk_inventory(data, directory):
        if data[:2] not in (b'TD', b'td'):
            raise backend.Failure('teledisk', 'The file has no TeleDisk signature.')
        archive = directory / 'source.td0'
        records = directory / 'td0-records'
        archive.write_bytes(data)
        adapter().native(directory, 'teledisk-records', archive, records)
        inventory = {}
        with records.open('rb') as stream:
            while header := stream.read(5):
                if len(header) != 5:
                    raise backend.Failure('teledisk', 'Incomplete TeleDisk sector inventory.')
                cylinder, head, sector = header[:3]
                size = int.from_bytes(header[3:], 'little')
                payload = stream.read(size)
                track = inventory.setdefault((cylinder, head), {})
                if len(payload) != size or not size or sector in track:
                    raise backend.Failure('teledisk', 'Invalid or duplicate TeleDisk sector.')
                track[sector] = payload
        return inventory

    def teledisk_raw(source, inventory, geometry, raw, *, verified_order=None):
        profile = source.with_name(source.name + '.mapping.json')
        try:
            count = geometry['sectors_per_track']
            size = geometry['sector_size']
            if geometry.get('boot_geometry') or geometry['offset'] or len(inventory) != geometry['tracks']:
                raise MappingError('TeleDisk tracks do not match the selected RAW definition.')
            if profile.is_file():
                mapping = PhysicalMapping.from_profile(json.loads(catalogue.read_text(profile)), geometry)
                order = [(t.cylinder, t.head, t.sectors) for t in mapping.tracks]
                mapping_source = 'sidecar'
            else:
                first = inventory.get((0, 0), {})
                ids = tuple(sorted(first))
                if (set(inventory) != {(c, 0) for c in range(geometry['tracks'])}
                        or ids not in (tuple(range(count)), tuple(range(1, count + 1)))):
                    raise MappingProfileRequired(f'Multi-sided or nonstandard TD0 layouts require {profile.name}.')
                order = [(c, 0, ids) for c in range(geometry['tracks'])]
                mapping_source = 'inferred'
            for cylinder, head, ids in order:
                sectors = inventory.get((cylinder, head), {})
                if set(sectors) != set(ids) or any(len(payload) != size for payload in sectors.values()):
                    raise MappingError(f'TeleDisk sectors do not match RAW geometry on track {cylinder}/{head}.')
            with raw.open('wb') as stream:
                for cylinder, head, ids in order:
                    for sector in ids:
                        stream.write(inventory[cylinder, head][sector])
            if verified_order is not None:
                verified_order.extend(order)
            return mapping_source
        except (OSError, ValueError, MappingError) as error:
            raise backend.Failure('physical_mapping', str(error)) from error

    def transfer(container, mapping, directory, raw, data, writing):
        map_path = directory / 'disk.map'
        map_path.write_text(''.join(
            ' '.join(map(str, (t.cylinder, t.head, t.mode, t.size, len(t.sectors), *t.sectors))) + '\n'
            for t in mapping.tracks), encoding='ascii')
        encoded = directory / ('image.' + container)
        if not writing:
            encoded.write_bytes(data)
        adapter().native(directory, 'mapped-write' if writing else 'mapped-read',
                         container, map_path, raw, encoded)
        return encoded

    def stage_image(source, data, container, geometry, directory, *, proposed_profile=None, observations=None):
        if container not in ('raw', 'dsk', *structured):
            raise backend.Failure('unsupported_container', 'Select RAW or standard CPCEMU DSK.')
        if data.startswith(b'EXTENDED CPC DSK File'):
            raise backend.Failure('unsupported_container', 'Extended CPCEMU DSK is outside the supported container scope.')
        if data.startswith(DSK_MAGIC) != (container == 'dsk'):
            raise backend.Failure('container_mismatch', 'The image header does not match the selected container.')
        image = directory / 'image.raw'
        mapping = None
        if container in structured:
            mapping = mapped_image(container, source, data, geometry, directory, image,
                                   proposed_profile=proposed_profile, observations=observations)
            end = image.stat().st_size
        elif container == 'dsk':
            check_standard_dsk(data, geometry, backend, observations=observations)
            encoded = directory / 'source.dsk'
            encoded.write_bytes(data)
            adapter().native(directory, 'decode', encoded, image)
            end = image.stat().st_size
        else:
            end = raw_extent(data, geometry, backend)
            image.write_bytes(data)
        return image, mapping, end

    class CandidateFailure(backend.Failure):
        def __init__(self, error, evaluation):
            super().__init__(error.code, error.message)
            self.evaluation = evaluation

    def evaluate_candidate(source, data, container, fmt, definition, geometry, directory, rid,
                           *, inventory=None, detecting=False):
        """Stage one interpretation in scratch storage and probe its directory read-only."""
        raw = directory / 'candidate.raw'
        layout = dict(kind='raw_extent' if container == 'raw' else 'physical_mapping')
        evaluation = dict(layout=layout, directory=dict(status='unavailable'))
        missing = 0
        mapping = None
        try:
            if container != 'raw':
                layout['expected'] = {key: geometry[key] for key in
                                      ('tracks', 'sectors_per_track', 'sector_size', 'offset')
                                      if key in geometry}
            if container == 'td0':
                if inventory is None:
                    inventory = teledisk_inventory(data, directory)
                layout['observed'] = dict(tracks=len(inventory))
                layout['mapping_source'] = teledisk_raw(source, inventory, geometry, raw)
                evidence = 'Verified TeleDisk sector mapping and readable directory'
            elif container in structured:
                raw.unlink(missing_ok=True)
                mapping = mapped_image(container, source, data, geometry, directory, raw,
                                       mapping_details=layout)
                evidence = 'Verified sector mapping and readable directory'
            elif container == 'dsk':
                check_standard_dsk(data, geometry, backend)
                if not raw.exists():
                    encoded = directory / 'source.dsk'
                    encoded.write_bytes(data)
                    decoded = directory / 'decoded.raw'
                    adapter().native(directory, 'decode', encoded, decoded)
                    decoded.replace(raw)
                evidence = 'Container geometry and readable directory'
            else:
                layout['observed'] = dict(bytes=len(data))
                end = raw_extent(data, geometry, backend)
                layout['expected'] = dict(bytes=end)
                if len(data) > end:
                    layout['trailing_bytes'] = len(data) - end
                    if detecting:
                        raise backend.Failure('image_size', 'The RAW image exceeds the selected definition extent.')
                if not raw.exists():
                    raw.write_bytes(data)
                missing = max(0, end - len(data))
                evidence = 'Exact RAW extent and readable directory'
                if missing:
                    evidence = 'Short RAW extent'
                elif 'trailing_bytes' in layout:
                    evidence = 'Readable directory within the selected RAW extent'
        except backend.Failure as error:
            if error.code == 'cancelled':
                raise
            reason = ('mapping_profile_required' if isinstance(error.__cause__, MappingProfileRequired)
                      else error.code)
            layout.update(status='failed', code=reason, message=error.message)
            raise CandidateFailure(error, evaluation) from error
        layout['status'] = 'passed'
        if container == 'raw':
            layout['missing_bytes'] = missing

        probe = adapter().request({
            'v': 1, 'id': rid, 'op': 'list', 'image': str(raw),
            'format': fmt, 'container': 'raw'}, defs_override=definition)
        if stop.is_set() or probe.get('error') == 'cancelled':
            raise backend.Failure('cancelled')
        if not probe['ok']:
            error = backend.Failure(probe['error'], probe.get('message', ''))
            evaluation['directory'] = dict(status='failed', code=error.code, message=error.message)
            raise CandidateFailure(error, evaluation)
        entries = probe['result']['entries']
        if not geometry.get('boot_geometry'):
            capacity = geometry['tracks'] * geometry['sectors_per_track'] * geometry['sector_size']
            if any(not 0 <= entry['size'] <= capacity for entry in entries):
                error = backend.Failure('image_incompatible', 'A reported file size exceeds the candidate capacity.')
                evaluation['directory'] = dict(status='failed', code=error.code, message=error.message)
                raise CandidateFailure(error, evaluation)
        evaluation['directory'] = dict(status='passed', entries=len(entries))
        if missing:
            directory_evidence = 'populated directory' if entries else 'empty directory'
            evidence += f' ({missing:,} bytes short) and {directory_evidence}'
        evaluation.update(evidence=evidence, rank=[int(bool(missing)), bool(missing and not entries), missing])
        return evaluation, probe['result'], mapping

    def validate_investigation_container(data, container):
        if data.startswith(b'EXTENDED CPC DSK File'):
            raise backend.Failure('unsupported_container', 'Extended CPCEMU DSK is outside the supported container scope.')
        if container not in ('raw', 'dsk', 'td0', *structured):
            raise backend.Failure('unsupported_container', f'Unsupported container: {container}')
        if data.startswith(DSK_MAGIC) != (container == 'dsk'):
            raise backend.Failure('container_mismatch', 'The image header does not match the selected container.')

    def check_filesystem(request):
        """Check a tab revision or a separately selected image under an explicit definition."""
        with tempfile.TemporaryDirectory(prefix='cpmimage-check-') as temp:
            directory = Path(temp)
            if 'session' in request:
                if any(key in request for key in ('image', 'format', 'container')):
                    raise backend.Failure('invalid_request', 'Choose either an active session or an external image.')
                context = dict(working_revision(request), mode='working', container='raw')
                working, definition = session['image'], session['definitions']
            else:
                fmt = request.get('format')
                if not isinstance(fmt, str) or fmt not in definitions:
                    raise backend.Failure('unknown_format', 'Select an explicit definition from the active catalogue.')
                if not isinstance(request.get('image'), str) or not request['image']:
                    raise backend.Failure('invalid_request', 'Choose an image to check.')
                source = Path(request['image']).absolute()
                data = read_image(source)
                container = image_container(source, data, request.get('container'))
                definition = definitions[fmt]
                context = dict(mode='external', source_name=source.name, format=fmt,
                               container=container, source_sha256=hashlib.sha256(data).hexdigest(),
                               definition_sha256=catalogue.digest(definition))
                try:
                    validate_investigation_container(data, container)
                    working = directory / 'image.raw'
                    if container == 'raw':
                        # The diagnostic reader reports missing directory or data
                        # sectors itself, including a partial final RAW sector.
                        working.write_bytes(data)
                    else:
                        geometry = describe(fmt, definition, directory)
                        if container == 'td0':
                            teledisk_raw(source, teledisk_inventory(data, directory), geometry, working)
                        else:
                            working, _, _ = stage_image(source, data, container, geometry, directory)
                except backend.Failure as error:
                    if stop.is_set() or error.code == 'cancelled':
                        raise backend.Failure('cancelled') from error
                    return dict(context, status='incomplete', phase='container',
                                code=error.code, message=error.message, findings=[], finding_count=0, omitted=0)
            if stop.is_set():
                raise backend.Failure('cancelled')
            (directory / 'diskdefs').write_text(definition, encoding='utf-8')
            report = adapter().native(directory, 'filesystem-check', working, context['format'], 'raw')
            if stop.is_set():
                raise backend.Failure('cancelled')
            report.pop('v')
            report.pop('ok')
            return dict(context, **report)

    def render_record(record):
        try:
            from disk_format import DiskFormatError, render_definition
        except ImportError as error:
            raise backend.Failure('editor_unavailable', 'Disk format validation requires jsonschema.') from error
        try:
            return render_definition(record)
        except DiskFormatError as error:
            raise backend.Failure('invalid_definition', str(error)) from error

    def merged_definitions(base, records):
        combined = base.copy()
        for record in records:
            rendered = render_record(record)
            if rendered.identifier in combined:
                raise backend.Failure('definition_conflict', f'Duplicate format identifier {rendered.identifier}.')
            combined[rendered.identifier] = rendered.text
        return combined

    def editor(request):
        nonlocal definitions, saved_loaded
        op = request['op']
        if op == 'definition_list':
            records, unsupported = store.records()
            return dict(records=records, unsupported=unsupported)
        if op == 'definition_import':
            text = request.get('text')
            if 'catalogue_identifier' in request:
                text = base_definitions.get(request['catalogue_identifier'])
                if not text:
                    raise backend.Failure('unknown_format', 'Select a standalone catalogue definition.')
            imported = catalogue.import_definition(text)
            return dict(record=imported.record, diagnostics=[asdict(item) for item in imported.diagnostics])

        record = request['record']
        rendered = render_record(record)
        if op in ('definition_save', 'definition_export') and 'import_source' in record:
            imported = catalogue.import_definition(record['import_source']['diskdefs_text'])
            unresolved = any(item.code != 'noncanonical_offset' for item in imported.diagnostics)
            if unresolved and request.get('resolved_import') is not True:
                raise backend.Failure('unresolved_import', 'Resolve the imported directives before saving or exporting.')
        fmt, definition = rendered.identifier, rendered.text
        with tempfile.TemporaryDirectory(prefix='cpmimage-editor-') as temp:
            directory = Path(temp)
            geometry = describe(fmt, definition, directory)
            result = dict(diskdefs=definition, definition_sha256=rendered.revision, geometry=geometry)
            if op in ('definition_check_image', 'mapping_preview', 'mapping_write'):
                source = Path(request['image']).absolute()
                data = read_image(source)
                container = image_container(source, data, request.get('container'))
                if op == 'definition_check_image':
                    validate_investigation_container(data, container)
                    evaluation, checked, mapping = evaluate_candidate(
                        source, data, container, fmt, definition, geometry, directory, request['id'])
                    result.update(checked, container=container, evaluation=evaluation,
                                  source_sha256=hashlib.sha256(data).hexdigest())
                    if mapping:
                        result['mapping'] = strict_profile(mapping)
                        result['mapping_source'] = evaluation['layout']['mapping_source']
                    return result
                profile = None
                if op == 'mapping_write':
                    if request.get('confirmed') is not True or container not in structured:
                        raise backend.Failure('confirmation_required', 'Confirm writing a structured image mapping.')
                    from disk_format import DiskFormatError, validate_mapping_sidecar
                    profile = request['profile']
                    try:
                        validate_mapping_sidecar(profile)
                    except DiskFormatError as error:
                        raise backend.Failure('invalid_mapping', str(error)) from error
                if op == 'mapping_preview':
                    if container not in structured:
                        raise backend.Failure('unsupported_container', 'Select an IMD, JV3, or DMK image.')
                    path = source.with_name(source.name + '.mapping.json')
                    original_data = mapping_bytes(path)
                    original_hash = mapping_hash(original_data)
                image, mapping, _ = stage_image(source, data, container, geometry, directory,
                                                proposed_profile=profile)
                if op == 'mapping_preview':
                    if mapping_bytes(path) != original_data:
                        raise backend.Failure('source_changed', 'The mapping changed. Preview it again.')
                    result.update(profile=strict_profile(mapping), mapping_sha256=original_hash,
                                  previous_profile=json.loads(original_data) if original_data is not None else None)
                else:
                    path = source.with_name(source.name + '.mapping.json')
                    contents = (json.dumps(profile, indent=2) + '\n').encode('utf-8')
                    if len(contents) > catalogue.MAX_CATALOGUE_BYTES:
                        contents = (json.dumps(profile, separators=(',', ':')) + '\n').encode('utf-8')
                    if len(contents) > catalogue.MAX_CATALOGUE_BYTES:
                        raise backend.Failure('invalid_mapping', 'Mapping exceeds the 256 KiB sidecar limit.')
                    store.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
                    with native_lock(store.directory / '.mapping-write.lock'):
                        if mapping_hash(mapping_bytes(path)) != request['mapping_sha256']:
                            raise backend.Failure('source_changed', 'The mapping changed. Preview it again.')
                        if read_image(source) != data:
                            raise backend.Failure('source_changed', 'The image changed. Check it again.')
                        if stop.is_set():
                            raise backend.Failure('cancelled')
                        atomic_write(path, contents)
                    result['mapping_sha256'] = mapping_hash(contents)
            elif op == 'definition_save':
                if stop.is_set():
                    raise backend.Failure('cancelled')
                records = store.save(record, bundled_identifiers | base_definitions.keys(),
                                     lambda records: merged_definitions(base_definitions, records),
                                     request.get('record_sha256'))
                result['record_sha256'] = hashlib.sha256(encode_record(record)).hexdigest()
                definitions = merged_definitions(base_definitions, records)
                saved_loaded = True
            return result

    def mapping_bytes(path):
        try:
            data = read_image(path)
        except FileNotFoundError:
            return None
        if len(data) > catalogue.MAX_CATALOGUE_BYTES:
            raise backend.Failure('invalid_mapping', 'Mapping exceeds the 256 KiB sidecar limit.')
        return data

    def mapping_hash(data):
        return hashlib.sha256(data).hexdigest() if data is not None else None

    def perform(request):
        nonlocal snapshot, session, definitions, base_definitions, saved_loaded
        rid = request['id']
        try:
            op = request.get('op')
            if not saved_loaded and op in ('inspect', 'open', 'new_image', 'filesystem_check'):
                definitions = merged_definitions(base_definitions, store.records()[0])
                saved_loaded = True
            if isinstance(op, str) and op in EDITOR_OPERATIONS:
                reply = dict(v=1, id=rid, ok=True, result=editor(request))
            elif op == 'catalogue':
                try:
                    path = Path(request['path']).resolve(strict=True) if request.get('path') else bundled_path
                    blocks = catalogue.definitions(catalogue.read_text(path), bundled=path == bundled_path)
                except (OSError, catalogue.CatalogueError) as error:
                    raise backend.Failure('invalid_catalogue', str(error)) from error
                if path == bundled_path:
                    blocks['amstrad'] = ''
                records, _ = store.records()
                combined = merged_definitions(blocks, records)
                rows = validate_catalogue(combined)
                names = {record['identifier']: record.get('display_name') for record in records}
                for row in rows:
                    if names.get(row['id']):
                        row['display_name'] = names[row['id']]
                base_definitions = blocks
                definitions = combined
                saved_loaded = True
                reply = dict(v=1, id=rid, ok=True, result=dict(formats=rows, catalogue=str(path)))
            elif op == 'inspect':
                working = 'session' in request
                if working and (not session or request['session'] != session['token']):
                    raise backend.Failure('stale_session', 'Open the image again.')
                source = session['image'] if working else Path(request['image']).absolute()
                data = read_image(source)
                container = 'raw' if working else image_container(source, data, request.get('container'))
                validate_investigation_container(data, container)
                explicit = request.get('explicit_format')
                if explicit and explicit not in definitions:
                    raise backend.Failure('unknown_format', f'{explicit} is absent from the selected catalogue.')
                fingerprint = hashlib.sha256(data).hexdigest()
                selected = None
                warning = ''
                if not request.get('explicit_format') and not all((key, catalogue.digest(value)) in descriptions
                    for key, value in definitions.items()):
                    validate_catalogue(definitions)
                with tempfile.TemporaryDirectory(prefix='cpmimage-inspect-') as temp:
                    directory = Path(temp)
                    if not working and not request.get('explicit_format'):
                        try:
                            selected = resolve_sidecar(source, directory)
                        except backend.Failure as error:
                            if error.code != 'invalid_sidecar':
                                raise
                            warning = error.message
                    inventory = teledisk_inventory(data, directory) if container == 'td0' else None
                    ranked, rejected = [], []
                    rejected_count = 0
                    mapping_warning = False
                    choices = () if selected or explicit else sorted(definitions.items())
                    for fmt, definition in choices:
                        if stop.is_set():
                            raise backend.Failure('cancelled')
                        geometry = describe(fmt, definition, directory)
                        if geometry.get('boot_geometry'):
                            continue
                        try:
                            evaluation, _, _ = evaluate_candidate(
                                source, data, container, fmt, definition, geometry, directory, rid,
                                inventory=inventory, detecting=True)
                        except CandidateFailure as error:
                            rejected_count += 1
                            stage = 'layout' if error.evaluation['layout']['status'] == 'failed' else 'directory'
                            mapping_warning |= stage == 'layout' and container in (*structured, 'td0')
                            if len(rejected) < 64:
                                finding = error.evaluation[stage]
                                rejected.append(dict(id=fmt, stage=stage, code=finding['code'],
                                                     detail=finding['message'][:240]))
                            continue
                        maker, model, family = metadata.get(fmt, ('', '', ''))
                        ranked.append((*evaluation['rank'], fmt, dict(id=fmt, manufacturer=maker,
                            model=model, family=family, evidence=evaluation['evidence'],
                            definition_sha256=catalogue.digest(definition), evaluation=evaluation)))
                    candidates = [row for *_, row in sorted(ranked)]
                result = dict(container=container, source_sha256=fingerprint, candidates=candidates,
                              rejected=rejected, rejected_count=rejected_count,
                              sidecar_warning=warning,
                              detection_warning=('No verified container mapping found during detection. '
                                                 'Select a definition to check its mapping and sector diagnostics.'
                                                 if mapping_warning and not candidates else ''))
                if selected:
                    fmt, definition, _ = selected
                    result['sidecar'] = dict(id=fmt, definition_sha256=catalogue.digest(definition))
                reply = dict(v=1, id=rid, ok=True, result=result)
                # Preserve the legacy picker rows if added diagnostics would exceed its reply limit.
                if len(json.dumps(reply)) + 1 > MAX_INSPECT_REPLY_BYTES:
                    for row in candidates:
                        row.pop('evaluation')
                    result.update(rejected=[], diagnostics_truncated=True)
            elif op == 'comparison_manifest':
                # Each tab produces its own paged manifest. Join files on the full
                # CP/M name (including user area) and directory rows on slot.
                context = working_revision(request)
                start, limit = request.get('start', 0), request.get('limit', 64)
                if (type(start) is not int or not 0 <= start <= 0x7fffffff
                        or type(limit) is not int or not 1 <= limit <= 128
                        or (start and 'revision' not in request)):
                    raise backend.Failure('invalid_request', 'Choose a valid comparison page and revision.')
                listing = comparison_files(context, rid)
                files = list(listing['files'].values())
                end = start + limit
                result = dict(context, rows=files[start:end], total=len(files), start=start,
                              next_start=end if end < len(files) else None,
                              free_bytes=listing['free_bytes'], total_bytes=listing['total_bytes'],
                              geometry=session['geometry'])
                reply = dict(v=1, id=rid, ok=True, result=result)
            elif op == 'comparison_fingerprints':
                # Only complete, unconverted logical file streams get fingerprints.
                context = working_revision(request)
                names = request.get('names')
                if ('revision' not in request or not isinstance(names, list) or not 1 <= len(names) <= 16
                        or not all(isinstance(name, str) for name in names)):
                    raise backend.Failure('invalid_request', 'Send a revision and 1 to 16 filenames.')
                names = [backend.filename(name, True) for name in names]
                files = comparison_files(context, rid)['files']
                if len(set(names)) != len(names) or any(name not in files for name in names):
                    raise backend.Failure('invalid_request', 'Choose distinct files from the comparison manifest.')
                rows = []
                with tempfile.TemporaryDirectory(prefix='cpmimage-compare-') as temp:
                    directory = Path(temp)
                    (directory / 'diskdefs').write_text(session['definitions'], encoding='utf-8')
                    output = directory / 'file.bin'
                    helper = adapter()
                    for name in names:
                        if stop.is_set():
                            raise backend.Failure('cancelled')
                        try:
                            helper.native(directory, 'export', session['image'], session['format'],
                                          'raw', name, output)
                            if not output.is_file() or output.stat().st_size != files[name]['size']:
                                raise backend.Failure('incomplete_read', 'The extracted size differs from the listing.')
                            fingerprint = file_hash(output)
                            if fingerprint is None:
                                raise backend.Failure('incomplete_read', 'The extracted file is unavailable.')
                            rows.append(dict(name=name, sha256=fingerprint))
                        except backend.Failure as error:
                            if error.code == 'cancelled':
                                raise
                            rows.append(dict(name=name, error=error.code))
                        finally:
                            output.unlink(missing_ok=True)
                if stop.is_set():
                    raise backend.Failure('cancelled')
                reply = dict(v=1, id=rid, ok=True, result=dict(context, rows=rows))
            elif op == 'comparison_classify':
                reply = dict(v=1, id=rid, ok=True, result=classify_comparison(request, backend))
            elif op == 'filesystem_check':
                reply = dict(v=1, id=rid, ok=True, result=check_filesystem(request))
            elif op == 'filesystem_structure':
                context = working_revision(request)
                view = request.get('view')
                start, limit = request.get('start', 0), request.get('limit', 128)
                block = request.get('block') if view == 'references' else 0
                if (view not in ('directory', 'extents', 'blocks', 'references')
                        or type(start) is not int or not 0 <= start <= 0x7fffffff
                        or type(limit) is not int or not 1 <= limit <= 128
                        or type(block) is not int or not 0 <= block <= 0x7fffffff):
                    raise backend.Failure('invalid_request', 'Choose a filesystem view and a valid page or block.')
                working = session['image']
                with tempfile.TemporaryDirectory(prefix='cpmimage-structure-') as temp:
                    directory = Path(temp)
                    (directory / 'diskdefs').write_text(session['definitions'], encoding='utf-8')
                    result = adapter().native(directory, 'filesystem-structure', working,
                                              session['format'], 'raw', view, start, limit, block)
                if stop.is_set():
                    raise backend.Failure('cancelled')
                result.pop('v')
                result.pop('ok')
                total = result['total']
                result.update(context, view=view, start=start,
                              next_start=start + limit if start + limit < total else None)
                if view == 'references':
                    result['block'] = block
                if view == 'blocks':
                    result['physical_links'] = bool(session.get('source_mapping'))
                reply = dict(v=1, id=rid, ok=True, result=result)
            elif op == 'source_metadata':
                # Omit track for the summary, or pass [cylinder, head] for ordered sectors.
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open an image first.')
                source_snapshot = session.get('source_metadata')
                result = dict(source=None, tracks=[])
                if 'block' in request:
                    working_revision(request)
                    block = request['block']
                    mapping = session.get('source_mapping')
                    if not mapping or source_snapshot is None:
                        raise backend.Failure('physical_mapping', 'No verified mapping to the original source is available.')
                    if type(block) is not int or not 0 <= block <= 0x7fffffff:
                        raise backend.Failure('invalid_request', 'Select an allocation block.')
                    with tempfile.TemporaryDirectory(prefix='cpmimage-sector-links-') as temp:
                        directory = Path(temp)
                        (directory / 'diskdefs').write_text(session['definitions'], encoding='utf-8')
                        slots = adapter().native(directory, 'filesystem-structure', session['image'],
                                                 session['format'], 'raw', 'sector_slots', 0, 1, block)['slots']
                    links = []
                    for track, slot in slots:
                        if not (0 <= track < len(mapping) and 0 <= slot < len(mapping[track][2])):
                            raise backend.Failure('physical_mapping', 'The block falls outside the verified source mapping.')
                        cylinder, head, ids = mapping[track]
                        links.append(dict(cylinder=cylinder, head=head, id=ids[slot]))
                    result.update(block=block, block_sectors=links)

                if source_snapshot is not None:
                    provenance, tracks = source_snapshot
                    result['source'] = provenance
                    if 'track' in request:
                        address = request['track']
                        if (not isinstance(address, list) or len(address) != 2
                                or any(type(value) is not int for value in address)):
                            raise backend.Failure('invalid_request', 'Specify a cylinder and head.')
                        key = tuple(address)
                        if key not in tracks:
                            raise backend.Failure('unknown_track', 'No source observations for that track.')
                        result.pop('tracks')
                        result.update(track=address, sectors=[
                            dict(id=sector, size=size, density=tracks.densities.get((*key, sector)),
                                 status=tracks.statuses.get((*key, sector)))
                            for sector, size in tracks[key]], mode=tracks.modes.get(key))
                    else:
                        result['tracks'] = [dict(cylinder=c, head=h, sector_count=len(sectors))
                                            for (c, h), sectors in sorted(tracks.items())]
                reply = dict(v=1, id=rid, ok=True, result=result)
            elif op == 'open':
                continuation = request.get('continue_session')
                previous = None
                if continuation is not None:
                    if not session or continuation != session['token']:
                        raise backend.Failure('stale_session', 'Open the image again.')
                    previous = session
                    if (previous['source'] is None
                            or previous['source'].resolve() != Path(request['image']).resolve()):
                        raise backend.Failure('source_changed', 'The image differs from the active session.')
                source = Path(request['image']).absolute()
                source_identity = identity(source.stat())
                data = read_image(source)
                fingerprint = hashlib.sha256(data).hexdigest()
                if ((previous and (previous['sha'] != fingerprint or previous['identity'] != source_identity))
                        or request.get('source_sha256', fingerprint) != fingerprint):
                    raise backend.Failure('source_changed', 'The image changed after format selection. Open it again.')
                fmt, container = request['format'], request['container']
                if source.suffix.lower() == '.td0':
                    container = 'td0'
                importing_td0 = container == 'td0'
                if not isinstance(fmt, str) or not fmt or len(fmt) > 200 or '\0' in fmt:
                    raise backend.Failure('unknown_format', 'Invalid disk-definition identifier.')
                if session and session['owner']:
                    session['owner'].close()
                session = None
                if snapshot:
                    snapshot.cleanup()
                    snapshot = None
                snapshot = tempfile.TemporaryDirectory(prefix='cpmimage-open-')
                directory = Path(snapshot.name)
                sidecar_selection = resolve_sidecar(source, directory) if request.get('sidecar') else None
                if sidecar_selection:
                    selected, definition, geometry = sidecar_selection
                    if selected != fmt:
                        raise backend.Failure('invalid_sidecar', 'The matching FDF identifier changed.')
                else:
                    if fmt not in definitions:
                        raise backend.Failure('unknown_format', f'{fmt} is absent from the selected catalogue.')
                    definition = definitions[fmt]
                    geometry = describe(fmt, definition, directory)
                if request.get('definition_sha256', catalogue.digest(definition)) != catalogue.digest(definition):
                    raise backend.Failure('definition_changed', 'The disk definition changed. Select it again.')
                observations = SourceTracks()
                source_mapping = []
                if importing_td0:
                    image = directory / 'image.raw'
                    inventory = teledisk_inventory(data, directory)
                    teledisk_raw(source, inventory, geometry, image, verified_order=source_mapping)
                    observations.update((key, tuple((sector, len(payload)) for sector, payload in sectors.items()))
                                        for key, sectors in inventory.items())
                    mapping, end = None, image.stat().st_size
                else:
                    image, mapping, end = stage_image(source, data, container, geometry, directory,
                                                      observations=observations)
                trimmed = image.stat().st_size < end
                reply = adapter().request({
                    'v': 1, 'id': rid, 'op': 'list', 'image': str(image),
                    'format': fmt, 'container': 'raw'}, defs_override=definition)
                if not reply['ok']:
                    respond(reply)
                    return
                if stop.is_set():
                    raise backend.Failure('cancelled')
                if trimmed:
                    # The native reader has checked every directory and allocated
                    # sector in the original short image before padding its tail.
                    with image.open('ab') as stream:
                        remaining = end - image.stat().st_size
                        while remaining:
                            chunk = min(remaining, 65536)
                            stream.write(b'\xe5' * chunk)
                            remaining -= chunk
                token = str(uuid.uuid4())
                session = dict(token=token, source=source, image=image, sha=fingerprint,
                               format=fmt, container='raw', definitions=definition,
                               original_container=container, owner=None, identity=source_identity,
                               geometry=geometry, dirty=False, confirmed=False, mapping=mapping,
                               template=data if container == 'dmk' else None,
                               comment=observations.imd_comment)
                order = dict(td0='scan', imd='sector_map', dmk='track_offset',
                             jv3='descriptor', dsk='descriptor')
                provenance = dict(name=source.name, container=container, sha256=fingerprint,
                                  sector_order=order.get(container))
                if container == 'imd':
                    provenance['comment'] = observations.imd_comment.decode('utf-8', errors='replace')
                if mapping is not None:
                    source_mapping = [(track.cylinder, track.head, track.sectors) for track in mapping.tracks]
                elif container == 'dsk':
                    source_mapping = [(c, h, tuple(sector for sector, _ in observations[c, h]))
                                      for c, h in sorted(observations)]
                session['source_mapping'] = source_mapping
                session['source_metadata'] = (provenance, observations)
                if previous:
                    session['source_metadata'] = previous.get('source_metadata')
                    if not session['source_metadata'] or session['source_metadata'][0]['sha256'] != fingerprint:
                        session['source_mapping'] = []
                    if 'archive_source' in previous:
                        session.update(archive_source=previous['archive_source'],
                                       archive_identity=previous['archive_identity'])
                if importing_td0:
                    session.update(source=None, identity=None, original_container='raw', dirty=True,
                                   archive_source=source, archive_identity=source_identity)
                    reply['result']['suggested_name'] = source.with_suffix('.raw').name
                reply['result'].update(session=token, source_sha256=fingerprint, unused_tail_filled=trimmed,
                                       definition_sha256=catalogue.digest(definition),
                                       imd_comment=session['comment'].decode('utf-8', errors='replace'), geometry=geometry)
            elif op in ('new_image', 'format_image'):
                creating = op == 'new_image'
                if not creating and (not session or request.get('session') != session['token']):
                    raise backend.Failure('stale_session', 'Open an image first.')
                if request.get('confirmed') is not True:
                    raise backend.Failure('confirmation_required', 'Confirm initialization first.')
                fmt = request['format'] if creating else session['format']
                if fmt == 'amstrad':
                    raise backend.Failure('unsupported_backend', 'Formatting requires a standalone disk definition.')
                definition = definitions.get(fmt) if creating else session['definitions']
                if definition is None:
                    raise backend.Failure('unknown_format', 'Select a catalogued disk definition.')
                revision = catalogue.digest(definition)
                if creating and request.get('definition_sha256', revision) != revision:
                    raise backend.Failure('definition_changed', 'The disk definition changed. Select it again.')
                label = request.get('label', '')
                if not isinstance(label, str) or len(label) > 11 or any(not 32 <= ord(c) <= 126 for c in label):
                    raise backend.Failure('invalid_label', 'Use up to 11 printable ASCII characters.')
                with tempfile.TemporaryDirectory(prefix='cpmimage-format-') as temp:
                    folder = Path(temp)
                    geometry = describe(fmt, definition, folder)
                    capacity = geometry['offset'] + geometry['tracks'] * geometry['sectors_per_track'] * geometry['sector_size']
                    if capacity > MAX_IMAGE_BYTES:
                        raise backend.Failure('input_limit', 'The definition exceeds the working-image limit.')
                    candidate = folder / 'working.raw'
                    if creating:
                        candidate.write_bytes(b'\xe5' * capacity)
                    else:
                        if session['image'].stat().st_size != capacity:
                            raise backend.Failure('image_incompatible', 'Formatting would change the image capacity.')
                        shutil.copyfile(session['image'], candidate)
                    (folder / 'diskdefs').write_text(definition, encoding='utf-8')
                    adapter().native(folder, 'initialize', candidate, fmt, label,
                                     '1' if request.get('timestamps') is True else '0')
                    if request.get('boot_path'):
                        install_system_tracks(candidate, geometry, request['boot_path'], request.get('pad') is True)
                    checked = adapter().request(dict(v=1,id=rid,op='list',image=str(candidate),
                        format=fmt,container='raw'), defs_override=definition)
                    if not checked['ok']:
                        raise backend.Failure(checked['error'], checked.get('message', ''))
                    if checked['result']['entries'] or candidate.stat().st_size != capacity:
                        raise backend.Failure('format_failed', 'Initialized filesystem failed validation.')
                    if stop.is_set():
                        raise backend.Failure('cancelled')
                    if creating:
                        new_snapshot = tempfile.TemporaryDirectory(prefix='cpmimage-open-')
                        working = Path(new_snapshot.name) / 'image.raw'
                        try:
                            shutil.copyfile(candidate, working)
                        except BaseException:
                            new_snapshot.cleanup()
                            raise
                        if session and session['owner']:
                            session['owner'].close()
                        if snapshot:
                            snapshot.cleanup()
                        snapshot = new_snapshot
                        session = dict(token=str(uuid.uuid4()), source=None, image=working, sha=None,
                            format=fmt, container='raw', definitions=definition, original_container='raw',
                            owner=None, identity=None, geometry=geometry, dirty=True, confirmed=True,
                            mapping=None, template=None, comment=b'')
                    else:
                        if session['source'] and session['owner'] is None:
                            owner = Ownership(session['source'], Path.home() / '.cpmimage' / 'ownership')
                            if owner.current != session['identity'] or file_hash(owner.path) != session['sha']:
                                owner.close()
                                raise backend.Failure('source_changed', 'The source changed. Open it again.')
                            session['owner'] = owner
                        working = session['image']
                        if not session['dirty']:
                            shutil.copyfile(working, working.with_name('saved.raw'))
                        # Keep replacement on the working image's filesystem.
                        staged = working.with_name('formatted.raw')
                        shutil.copyfile(candidate, staged)
                        os.replace(staged, working)
                        session.update(dirty=True, confirmed=True)
                reply = dict(v=1,id=rid,ok=True,result=dict(checked['result'], dirty=True, session=session['token'], geometry=geometry))
            elif op == 'imd_comment':
                if not session or request.get('session') != session['token'] or session['original_container'] != 'imd':
                    raise backend.Failure('stale_session', 'Open an IMD image.')
                comment = request['comment'].encode('utf-8')
                if len(comment) > 60000 or b'\0' in comment or b'\x1a' in comment:
                    raise backend.Failure('invalid_comment', 'Invalid or oversized IMD comment.')
                if comment != session['comment']:
                    if not session['dirty']:
                        shutil.copyfile(session['image'], session['image'].with_name('saved.raw'))
                    session.setdefault('saved_comment', session['comment'])
                    session.update(comment=comment, dirty=True)
                reply = dict(v=1, id=rid, ok=True, result=dict(dirty=session['dirty'], imd_comment=request['comment']))
            elif op == 'edit':
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                if not session['confirmed'] and request.get('confirmed') is not True:
                    raise backend.Failure('confirmation_required', 'Confirm the selected disk definition for writing.')
                action = request['action']
                if action not in ('rename', 'delete', 'delete_many', 'attrs', 'import', 'move'):
                    raise backend.Failure('invalid_edit', 'Unknown editing operation.')
                name = '' if action in ('import', 'delete_many') else backend.filename(request['name'], True)
                argument = '-'
                if action in ('rename', 'move'):
                    argument = backend.filename(request['target'], True)
                    if action == 'move' and name[2:] != argument[2:]:
                        raise backend.Failure('invalid_edit', 'Moving a file preserves its filename.')
                    action = 'rename'
                elif action == 'attrs':
                    attributes = request['attributes']
                    if type(attributes) is not int or attributes & ~0x700:
                        raise backend.Failure('invalid_edit', 'Only RO, SYS and ARC can be edited.')
                    argument = str(attributes)
                working = session['image']
                if session['source'] and not session['owner']:
                    owner = Ownership(session['source'], Path.home() / '.cpmimage' / 'ownership')
                    if owner.current != session['identity'] or file_hash(owner.path) != session['sha']:
                        owner.close()
                        raise backend.Failure('source_changed', 'The source changed. Open it again.')
                    session['owner'] = owner
                with tempfile.TemporaryDirectory(prefix='cpmimage-edit-', dir=working.parent) as temp:
                    edit_dir = Path(temp)
                    candidate = edit_dir / 'edited.raw'
                    shutil.copyfile(working, candidate)
                    (edit_dir / 'diskdefs').write_text(session['definitions'], encoding='utf-8')
                    if action == 'import':
                        items = request.get('items', [request])
                        if not isinstance(items, list) or not 1 <= len(items) <= 256:
                            raise backend.Failure('invalid_edit', 'Import batches support 1 to 256 files.')
                        names = set()
                        for item in items:
                            target = backend.filename(item['name'], True)
                            if target in names:
                                raise backend.Failure('invalid_edit', f'Duplicate import name: {target}')
                            names.add(target)
                            host = Path(item['path'])
                            info = host.stat()
                            data = read_image(host)
                            if item.get('text') is True:
                                data = data.replace(b'\r\n', b'\n').replace(b'\r', b'\n').replace(b'\n', b'\r\n') + b'\x1a'
                            staged = edit_dir / 'input.bin'
                            staged.write_bytes(data)
                            os.utime(staged, ns=(info.st_atime_ns, info.st_mtime_ns))
                            if item.get('replace') is True:
                                adapter().native(edit_dir, 'edit_delete', candidate, session['format'], 'raw', target, '-')
                            adapter().native(edit_dir, 'edit_import', candidate, session['format'], 'raw', target, staged)
                    elif action == 'delete_many':
                        names = request['names']
                        if not isinstance(names, list) or not 1 <= len(names) <= 256:
                            raise backend.Failure('invalid_edit', 'Delete batch must contain 1 to 256 names.')
                        for item in names:
                            target = backend.filename(item, True)
                            adapter().native(edit_dir, 'edit_delete', candidate, session['format'], 'raw', target, '-')
                    else:
                        adapter().native(edit_dir, 'edit_' + action, candidate, session['format'], 'raw', name, argument)
                    checked = adapter().request({
                        'v': 1, 'id': rid, 'op': 'list', 'image': str(candidate),
                        'format': session['format'], 'container': 'raw'}, defs_override=session['definitions'])
                    if not checked['ok']:
                        raise backend.Failure(checked['error'], checked.get('message', ''))
                    if stop.is_set():
                        raise backend.Failure('cancelled')
                    if not session['dirty']:
                        shutil.copyfile(working, working.with_name('saved.raw'))
                    os.replace(candidate, working)
                session.update(dirty=True, confirmed=True)
                reply = checked
                reply['result']['dirty'] = True
            elif op in ('systems_export', 'systems_import', 'discard'):
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                working = session['image']
                baseline = working.with_name('saved.raw')
                if op == 'discard':
                    if session['dirty'] and not session['source']:
                        session = None
                        snapshot.cleanup()
                        snapshot = None
                        respond(dict(v=1,id=rid,ok=True,result=dict(closed=True,dirty=False)))
                        return
                    if session['dirty']:
                        checked = adapter().request({
                            'v': 1, 'id': rid, 'op': 'list', 'image': str(baseline),
                            'format': session['format'], 'container': 'raw'}, defs_override=session['definitions'])
                        if not checked['ok']:
                            raise backend.Failure(checked['error'], checked.get('message', ''))
                        os.replace(baseline, working)
                    session.update(dirty=False, confirmed=False, comment=session.pop('saved_comment', session['comment']))
                    if session['owner']:
                        session['owner'].close()
                        session['owner'] = None
                else:
                    geo = session['geometry']
                    start = geo.get('offset', 0)
                    size = geo.get('boot_sectors', 0) * geo.get('sector_size', 0)
                    if not size or start + size > working.stat().st_size:
                        raise backend.Failure('unsupported_backend', 'No accessible reserved system-track region.')
                    path = Path(request['path'])
                    if op == 'systems_export':
                        with working.open('rb') as stream:
                            stream.seek(start)
                            data = stream.read(size)
                        with path.open('xb') as stream:
                            stream.write(data)
                    else:
                        if request.get('confirmed') is not True and not session['confirmed']:
                            raise backend.Failure('confirmation_required', 'Confirm the selected disk definition for writing.')
                        if session['source'] and not session['owner']:
                            owner = Ownership(session['source'], Path.home() / '.cpmimage' / 'ownership')
                            if owner.current != session['identity'] or file_hash(owner.path) != session['sha']:
                                owner.close()
                                raise backend.Failure('source_changed', 'The source changed. Open it again.')
                            session['owner'] = owner
                        candidate = working.with_name('edit.raw')
                        shutil.copyfile(working, candidate)
                        try:
                            install_system_tracks(candidate, geo, path, request.get('pad') is True)
                            if stop.is_set():
                                raise backend.Failure('cancelled')
                            if not session['dirty']:
                                shutil.copyfile(working, baseline)
                            os.replace(candidate, working)
                        finally:
                            if candidate.exists():
                                candidate.unlink()
                        session.update(dirty=True, confirmed=True)
                result = dict(dirty=session['dirty'], imd_comment=session['comment'].decode('utf-8', errors='replace'))
                if op == 'discard' and 'checked' in locals():
                    result.update(checked['result'])
                reply = dict(v=1, id=rid, ok=True, result=result)
            elif op in ('save_plan', 'save'):
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                source = session['source']
                if not source and not request.get('destination'):
                    raise backend.Failure('invalid_destination', 'Choose a destination for the new image.')
                destination = Path(request.get('destination') or source).absolute().resolve()
                if destination.suffix.lower() == '.td0':
                    raise backend.Failure('unsupported_container', 'TeleDisk is import-only. Choose a RAW filename.')
                require_separate_archive(session, destination)
                same = source is not None and source.resolve() == destination
                if source and not same and destination.exists() and os.path.samefile(source, destination):
                    raise backend.Failure('invalid_destination', 'Save As requires a separate file, not an alias of the source.')
                expected = file_hash(destination)
                recovery = recovery_directory(destination)
                if recovery.exists():
                    raise backend.Failure('recovery_required', f'Review the interrupted save in {recovery}.')
                if same and (identity(destination.stat()) != session['identity'] or expected != session['sha']):
                    raise backend.Failure('source_changed', 'The source changed. Open it again or use Save As.')
                output_container = request.get('container') or session['original_container']
                if output_container not in ('raw', 'dsk', *structured) or (same and output_container != session['original_container']):
                    raise backend.Failure('unsupported_container', 'Choose a supported container using Save As.')
                sidecar = None
                if request.get('sidecar', True):
                    definition = session['definitions']
                    if not definition:
                        raise backend.Failure('unsupported_backend', 'This format has no standalone definition.')
                    target = catalogue.sidecar(destination) or destination.with_suffix('.FDF')
                    require_separate_archive(session, target)
                    if target == destination:
                        raise backend.Failure('invalid_destination', 'Choose an image filename distinct from its FDF.')
                    require_recovery_clear(destination, [target])
                    previous = file_hash(target)
                    digest = hashlib.sha256(definition.encode('utf-8')).hexdigest()
                    if digest != previous:
                        sidecar = (target, previous)
                plan = dict(destination=str(destination), expected=expected, container=output_container,
                            sidecar_changed=sidecar is not None,
                            sidecar_path=str(sidecar[0]) if sidecar else None,
                            sidecar_expected=sidecar[1] if sidecar else None,
                            backups=request.get('backups', True))
                if op == 'save':
                    if request.get('plan') != plan:
                        raise backend.Failure('save_plan_changed', 'Review the save again before publishing.')
                    if not same and expected is not None and request.get('replace') is not True:
                        raise backend.Failure('destination_exists', 'Confirm replacement of the existing image.')
                    if sidecar and request.get('confirm_sidecar') is not True:
                        raise backend.Failure('confirmation_required', 'Confirm publication of the selected definition.')
                    owner = session['owner'] if same else None
                    acquired = owner is None
                    if acquired:
                        owner = Ownership(source if same else destination, Path.home() / '.cpmimage' / 'ownership')
                    try:
                        if same and owner.current != session['identity']:
                            raise backend.Failure('source_changed', 'The source identity changed.')
                        with tempfile.TemporaryDirectory(prefix='cpmimage-save-') as temp:
                            staged = None
                            if sidecar:
                                staged = Path(temp) / 'definition'
                                staged.write_bytes(definition.encode('utf-8'))
                            if stop.is_set():
                                raise backend.Failure('cancelled')
                            output = session['image'] if not same or session['dirty'] else None
                            if output is not None:
                                checked = adapter().request({
                                    'v': 1, 'id': rid, 'op': 'list', 'image': str(output),
                                    'format': session['format'], 'container': 'raw'}, defs_override=session['definitions'])
                                if not checked['ok']:
                                    raise backend.Failure(checked['error'], checked.get('message', ''))
                            if output is not None and output_container == 'dsk':
                                folder = Path(temp)
                                encoded = folder / 'output.dsk'
                                decoded = folder / 'verified.raw'
                                adapter().native(folder, 'encode', output, encoded)
                                check_standard_dsk(encoded.read_bytes(), session['geometry'], backend)
                                adapter().native(folder, 'decode', encoded, decoded)
                                if file_hash(decoded) != file_hash(output):
                                    raise backend.Failure('image_incompatible', 'Encoded DSK failed its byte-for-byte RAW round trip.')
                                output = encoded
                            if output is not None and output_container in structured:
                                folder = Path(temp)
                                try:
                                    profile = destination.with_name(destination.name + '.mapping.json')
                                    if same and session['mapping'] is not None:
                                        mapping = session['mapping']
                                    else:
                                        mapping = PhysicalMapping.from_profile(
                                            json.loads(catalogue.read_text(profile)), session['geometry'])
                                    if session['mapping'] is not None and mapping != session['mapping']:
                                        raise MappingError('Destination profile differs from the source RAW slot mapping.')
                                    if output_container == 'dmk':
                                        template = session['template'] or dmk_template(mapping)
                                        encoded = folder / 'image.dmk'
                                        encoded.write_bytes(transfer_dmk(template, mapping, output.read_bytes()))
                                    else:
                                        encoded = transfer(output_container, mapping, folder, output, None, True)
                                except (MappingError, OSError, ValueError) as error:
                                    raise backend.Failure('physical_mapping', str(error)) from error
                                verified = folder / 'verified.raw'
                                encoded_bytes = encoded.read_bytes()
                                if output_container == 'dmk':
                                    verified.write_bytes(transfer_dmk(encoded_bytes, mapping))
                                else:
                                    if output_container == 'imd':
                                        encoded_bytes = replace_imd_comment(encoded_bytes, session['comment'])
                                        encoded.write_bytes(encoded_bytes)
                                        mapping.verify_inventory(inspect_imd(encoded_bytes)[0])
                                    if output_container == 'jv3':
                                        verify_jv3(encoded_bytes, mapping)
                                    transfer(output_container, mapping, folder, verified, encoded_bytes, False)
                                if file_hash(verified) != file_hash(output):
                                    raise backend.Failure('image_incompatible', 'Container failed its RAW round trip.')
                                output = encoded
                            if sidecar:
                                with Ownership(sidecar[0], Path.home() / '.cpmimage' / 'ownership') as sidecar_owner:
                                    sidecar_owner.check(replacing=True)
                                    publish(owner, expected, output, (*sidecar, staged), request.get('backups', True))
                            else:
                                publish(owner, expected, output, backups=request.get('backups', True))
                    except BaseException:
                        if acquired:
                            owner.close()
                        raise
                    if session['owner'] and session['owner'] is not owner:
                        session['owner'].close()
                    session.update(owner=owner, source=owner.path, identity=owner.current, sha=file_hash(owner.path), dirty=False, original_container=output_container)
                    if output is not None and output_container in structured:
                        session['mapping'] = mapping
                    session.pop('saved_comment', None)
                    plan['source_sha256'] = session['sha']
                reply = dict(v=1, id=rid, ok=True, result=plan)
            elif op == 'preview':
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                name = backend.filename(request['name'], True)
                destination = Path(request['destination'])
                with tempfile.TemporaryDirectory(prefix='cpmimage-preview-') as temp:
                    folder = Path(temp)
                    staged = folder / 'preview'
                    (folder / 'diskdefs').write_text(session['definitions'], encoding='utf-8')
                    result = adapter().native(folder, 'preview', session['image'], session['format'],
                                              'raw', name, staged)
                    if type(result.get('truncated')) is not bool or staged.stat().st_size > 1024 * 1024:
                        raise backend.Failure('protocol_invalid', 'Invalid text preview.')
                    if stop.is_set():
                        raise backend.Failure('cancelled')
                    with destination.open('xb') as stream:
                        stream.write(staged.read_bytes())
                reply = dict(v=1, id=rid, ok=True, result=dict(truncated=result['truncated']))
            elif op == 'export':
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                if session['source'] and hashlib.sha256(read_image(session['source'])).hexdigest() != session['sha']:
                    raise backend.Failure('source_changed', 'The source changed. Open it again.')
                reply = adapter().request({
                    'v': 1, 'id': rid, 'op': 'export', 'image': str(session['image']),
                    'format': session['format'], 'container': session['container'],
                    'name': request['name'], 'destination': request['destination'],
                    'text': request.get('text', False)}, defs_override=session['definitions'])
            else:
                raise backend.Failure('read_only', 'Only read-only operations are available.')
            respond(reply)
        except CandidateFailure as error:
            respond(dict(v=1, id=rid, ok=False, error=error.code, message=error.message,
                         evaluation=error.evaluation))
        except backend.Failure as error:
            respond(dict(v=1, id=rid, ok=False, error=error.code, message=error.message))
        except DefinitionStoreError as error:
            respond(dict(v=1, id=rid, ok=False, error='definition_store', message=str(error)))
        except OwnershipError as error:
            respond(dict(v=1, id=rid, ok=False, error='ownership', message=str(error)))
        except (OSError, KeyError, TypeError, ValueError) as error:
            respond(dict(v=1, id=rid, ok=False, error='request_or_io', message=str(error)))

    # EOF or GUI termination cancels/reaps the current helper before broker exit.
    def interrupted(signum, frame):
        stop.set()
        raise EOFError()
    signal.signal(signal.SIGTERM, interrupted)
    try:
        while True:
            raw = sys.stdin.buffer.readline(MAX_EDITOR_REQUEST_BYTES + 1)
            if not raw:
                break
            if len(raw) > MAX_EDITOR_REQUEST_BYTES or not raw.endswith(b'\n'):
                break  # Never parse an incomplete/oversized request.
            try:
                request = json.loads(raw)
                if not isinstance(request, dict) or request.get('v') != 1:
                    raise ValueError('Invalid protocol version')
                if len(raw) > 65536 and request.get('op') not in tuple(EDITOR_OPERATIONS):
                    break
                rid = request.get('id')
                if not isinstance(rid, str) or not 0 < len(rid) <= 128:
                    raise ValueError('Invalid request ID')
                if request.get('op') == 'cancel':
                    if request.get('target') == active_id:
                        stop.set()
                    continue
                if worker and worker.is_alive() and not completed.is_set():
                    emit(dict(v=1, id=rid, ok=False, error='busy'))
                    continue
                if worker:
                    worker.join()
                active_id = rid
                completed.clear()
                stop.clear()
                worker = threading.Thread(target=perform, args=(request,))
                worker.start()
            except (ValueError, UnicodeError) as error:
                emit(dict(v=1, id=None, ok=False, error='invalid_request', message=str(error)))
    except EOFError:
        pass
    finally:
        stop.set()
        if worker:
            worker.join()
        if session and session['owner']:
            session['owner'].close()
        if snapshot:
            snapshot.cleanup()


if __name__ == '__main__':
    main()
