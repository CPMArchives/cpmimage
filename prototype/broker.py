# SPDX-License-Identifier: GPL-3.0-or-later
"""Local review prototype: retained snapshots, read-only helper, bounded JSON lines.

Reuses the qualified experimental parent, not a new filesystem implementation.
Only one operation runs at a time; cancellation is cooperative through the parent.
"""
import argparse
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
DSK_MAGIC = b'MV - CPCEMU Disk-File\r\nDisk-Info\r\n'


def check_standard_dsk(data, geometry, backend):
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--helper', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.root / 'experiments/process-comparison'))
    sys.path.insert(0, str(args.root / 'prototype'))
    import catalogue
    from publication import fingerprint as file_hash, publish, recovery_directory, require_recovery_clear
    from source_ownership import Ownership, identity
    import adapters as backend
    backend.HELPER = args.helper.resolve()
    bundled_path = args.root / 'fixtures/diskdefs'
    definitions = catalogue.definitions(catalogue.read_text(bundled_path), bundled=True)
    definitions['amstrad'] = ''
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

    def perform(request):
        nonlocal snapshot, session, definitions
        rid = request['id']
        try:
            op = request.get('op')
            if op == 'catalogue':
                try:
                    path = Path(request['path']).resolve(strict=True) if request.get('path') else bundled_path
                    blocks = catalogue.definitions(catalogue.read_text(path), bundled=path == bundled_path)
                except (OSError, catalogue.CatalogueError) as error:
                    raise backend.Failure('invalid_catalogue', str(error)) from error
                if path == bundled_path:
                    blocks['amstrad'] = ''
                rows = validate_catalogue(blocks)
                definitions = blocks
                reply = dict(v=1, id=rid, ok=True, result=dict(formats=rows, catalogue=str(path)))
            elif op == 'inspect':
                source = Path(request['image']).absolute()
                data = read_image(source)
                container = 'dsk' if data.startswith(DSK_MAGIC) else 'raw'
                if data.startswith(b'EXTENDED CPC DSK File'):
                    raise backend.Failure('unsupported_container', 'Extended CPCEMU DSK is outside the supported container scope.')
                fingerprint = hashlib.sha256(data).hexdigest()
                selected = None
                warning = ''
                if not request.get('explicit_format') and not all((key, catalogue.digest(value)) in descriptions
                    for key, value in definitions.items()):
                    validate_catalogue(definitions)
                with tempfile.TemporaryDirectory(prefix='cpmimage-inspect-') as temp:
                    directory = Path(temp)
                    if not request.get('explicit_format'):
                        try:
                            selected = resolve_sidecar(source, directory)
                        except backend.Failure as error:
                            if error.code != 'invalid_sidecar':
                                raise
                            warning = error.message
                    candidates = []
                    raw = directory / 'candidate.raw'
                    ranked = []
                    choices = () if selected or request.get('explicit_format') else sorted(definitions.items())
                    for fmt, definition in choices:
                        if stop.is_set():
                            raise backend.Failure('cancelled')
                        geometry = describe(fmt, definition, directory)
                        if geometry.get('boot_geometry'):
                            continue
                        if container == 'dsk':
                            try:
                                check_standard_dsk(data, geometry, backend)
                            except backend.Failure:
                                continue
                        else:
                            try:
                                if raw_extent(data, geometry, backend) != len(data):
                                    continue
                            except backend.Failure:
                                continue
                        if not raw.exists():
                            if container == 'dsk':
                                encoded = directory / 'source.dsk'
                                encoded.write_bytes(data)
                                adapter().native(directory, 'decode', encoded, raw)
                            else:
                                raw.write_bytes(data)
                        probe = adapter().request({
                            'v': 1, 'id': rid, 'op': 'list', 'image': str(raw),
                            'format': fmt, 'container': 'raw'}, defs_override=definition)
                        if probe['ok']:
                            populated = bool(probe['result']['entries'])
                            label = 'Geometry and nonempty directory' if populated else 'Geometry and empty directory'
                            maker, model, family = metadata.get(fmt, ('', '', ''))
                            ranked.append((not populated, fmt, dict(id=fmt, manufacturer=maker, model=model, family=family,
                                evidence=label, definition_sha256=catalogue.digest(definition))))
                    candidates = [row for _, _, row in sorted(ranked)]
                result = dict(container=container, source_sha256=fingerprint, candidates=candidates,
                              sidecar_warning=warning)
                if selected:
                    fmt, definition, _ = selected
                    result['sidecar'] = dict(id=fmt, definition_sha256=catalogue.digest(definition))
                reply = dict(v=1, id=rid, ok=True, result=result)
            elif op == 'open':
                if session and session['owner']:
                    session['owner'].close()
                session = None
                if snapshot:
                    snapshot.cleanup()
                    snapshot = None
                source = Path(request['image']).absolute()
                source_identity = identity(source.stat())
                data = read_image(source)
                fingerprint = hashlib.sha256(data).hexdigest()
                if request.get('source_sha256', fingerprint) != fingerprint:
                    raise backend.Failure('source_changed', 'The image changed after format selection. Open it again.')
                fmt, container = request['format'], request['container']
                if not isinstance(fmt, str) or not fmt or len(fmt) > 200 or '\0' in fmt:
                    raise backend.Failure('unknown_format', 'Invalid disk-definition identifier.')
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
                if container not in ('raw', 'dsk'):
                    raise backend.Failure('unsupported_container', 'Select RAW or standard CPCEMU DSK.')
                if data.startswith(b'EXTENDED CPC DSK File'):
                    raise backend.Failure('unsupported_container', 'Extended CPCEMU DSK is outside the supported container scope.')
                if data.startswith(DSK_MAGIC) != (container == 'dsk'):
                    raise backend.Failure('container_mismatch', 'The image header does not match the selected container.')
                image = directory / 'image.raw'
                if container == 'dsk':
                    check_standard_dsk(data, geometry, backend)
                    encoded = directory / 'source.dsk'
                    encoded.write_bytes(data)
                    adapter().native(directory, 'decode', encoded, image)
                    end = image.stat().st_size
                else:
                    end = raw_extent(data, geometry, backend)
                    image.write_bytes(data)
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
                               geometry=geometry, dirty=False, confirmed=False)
                reply['result'].update(session=token, source_sha256=fingerprint, unused_tail_filled=trimmed,
                                       definition_sha256=catalogue.digest(definition))
            elif op == 'edit':
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                if not session['confirmed'] and request.get('confirmed') is not True:
                    raise backend.Failure('confirmation_required', 'Confirm the selected disk definition for writing.')
                action = request['action']
                if action not in ('rename', 'delete', 'attrs', 'import', 'move'):
                    raise backend.Failure('invalid_edit', 'Unknown editing operation.')
                name = '' if action == 'import' else backend.filename(request['name'], True)
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
                if not session['owner']:
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
                    if session['dirty']:
                        checked = adapter().request({
                            'v': 1, 'id': rid, 'op': 'list', 'image': str(baseline),
                            'format': session['format'], 'container': 'raw'}, defs_override=session['definitions'])
                        if not checked['ok']:
                            raise backend.Failure(checked['error'], checked.get('message', ''))
                        os.replace(baseline, working)
                    session.update(dirty=False, confirmed=False)
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
                        with path.open('rb') as stream:
                            data = stream.read(size + 1)
                        if len(data) > size:
                            raise backend.Failure('input_limit', 'System-track input exceeds the reserved region.')
                        if len(data) < size and request.get('pad') is not True:
                            raise backend.Failure('padding_required', 'Confirm E5 padding of the short system-track input.')
                        if not session['owner']:
                            owner = Ownership(session['source'], Path.home() / '.cpmimage' / 'ownership')
                            if owner.current != session['identity'] or file_hash(owner.path) != session['sha']:
                                owner.close()
                                raise backend.Failure('source_changed', 'The source changed. Open it again.')
                            session['owner'] = owner
                        candidate = working.with_name('edit.raw')
                        shutil.copyfile(working, candidate)
                        try:
                            with candidate.open('r+b') as stream:
                                stream.seek(start)
                                stream.write(data.ljust(size, b'\xe5'))
                            if stop.is_set():
                                raise backend.Failure('cancelled')
                            if not session['dirty']:
                                shutil.copyfile(working, baseline)
                            os.replace(candidate, working)
                        finally:
                            if candidate.exists():
                                candidate.unlink()
                        session.update(dirty=True, confirmed=True)
                result = dict(dirty=session['dirty'])
                if op == 'discard' and 'checked' in locals():
                    result['entries'] = checked['result']['entries']
                reply = dict(v=1, id=rid, ok=True, result=result)
            elif op in ('save_plan', 'save'):
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                source = session['source']
                destination = Path(request.get('destination') or source).absolute().resolve()
                same = source.resolve() == destination
                if not same and destination.exists() and os.path.samefile(source, destination):
                    raise backend.Failure('invalid_destination', 'Save As requires a separate file, not an alias of the source.')
                expected = file_hash(destination)
                recovery = recovery_directory(destination)
                if recovery.exists():
                    raise backend.Failure('recovery_required', f'Review the interrupted save in {recovery}.')
                if same and (identity(destination.stat()) != session['identity'] or expected != session['sha']):
                    raise backend.Failure('source_changed', 'The source changed. Open it again or use Save As.')
                output_container = request.get('container') or session['original_container']
                if output_container not in ('raw', 'dsk') or (same and output_container != session['original_container']):
                    raise backend.Failure('unsupported_container', 'Choose a supported container using Save As.')
                sidecar = None
                if request.get('sidecar', True):
                    definition = session['definitions']
                    if not definition:
                        raise backend.Failure('unsupported_backend', 'This format has no standalone definition.')
                    target = catalogue.sidecar(destination) or destination.with_suffix('.FDF')
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
                    plan['source_sha256'] = session['sha']
                reply = dict(v=1, id=rid, ok=True, result=plan)
            elif op == 'export':
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                if hashlib.sha256(read_image(session['source'])).hexdigest() != session['sha']:
                    raise backend.Failure('source_changed', 'The source changed. Open it again.')
                reply = adapter().request({
                    'v': 1, 'id': rid, 'op': 'export', 'image': str(session['image']),
                    'format': session['format'], 'container': session['container'],
                    'name': request['name'], 'destination': request['destination'],
                    'text': request.get('text', False)}, defs_override=session['definitions'])
            else:
                raise backend.Failure('read_only', 'Only read-only operations are available.')
            respond(reply)
        except backend.Failure as error:
            respond(dict(v=1, id=rid, ok=False, error=error.code, message=error.message))
        except (OSError, KeyError, TypeError, ValueError) as error:
            respond(dict(v=1, id=rid, ok=False, error='request_or_io', message=str(error)))

    # EOF or GUI termination cancels/reaps the current helper before broker exit.
    def interrupted(signum, frame):
        stop.set()
        raise EOFError()
    signal.signal(signal.SIGTERM, interrupted)
    try:
        while True:
            raw = sys.stdin.buffer.readline(65537)
            if not raw:
                break
            if len(raw) > 65536 or not raw.endswith(b'\n'):
                break  # Never parse an incomplete/oversized request.
            try:
                request = json.loads(raw)
                if not isinstance(request, dict) or request.get('v') != 1:
                    raise ValueError('Invalid protocol version')
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
