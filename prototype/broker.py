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
    import adapters as backend
    backend.HELPER = args.helper.resolve()
    definitions = (args.root / 'fixtures/diskdefs').read_text(encoding='utf-8')
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

    def perform(request):
        nonlocal snapshot, session
        rid = request['id']
        try:
            op = request.get('op')
            if op not in ('open', 'export'):
                raise backend.Failure('read_only', 'Only listing and export are available.')
            if op == 'open':
                session = None
                if snapshot:
                    snapshot.cleanup()
                    snapshot = None
                source = Path(request['image']).resolve(strict=True)
                data = read_image(source)
                fmt, container = request['format'], request['container']
                if not isinstance(fmt, str) or not fmt or len(fmt) > 200 or '\0' in fmt:
                    raise backend.Failure('unknown_format', 'Invalid disk-definition identifier.')
                snapshot = tempfile.TemporaryDirectory(prefix='cpmimage-open-')
                directory = Path(snapshot.name)
                (directory / 'diskdefs').write_text(definitions, encoding='utf-8')
                if fmt not in descriptions:
                    descriptions[fmt] = adapter().native(directory, 'describe', fmt)
                geometry = descriptions[fmt]
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
                    'format': fmt, 'container': 'raw'}, defs_override=definitions)
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
                fingerprint = hashlib.sha256(data).hexdigest()
                session = dict(token=token, source=source, image=image, sha=fingerprint,
                               format=fmt, container='raw', definitions=definitions)
                reply['result'].update(session=token, source_sha256=fingerprint, unused_tail_filled=trimmed)
            else:
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                if hashlib.sha256(read_image(session['source'])).hexdigest() != session['sha']:
                    raise backend.Failure('source_changed', 'The source changed. Open it again.')
                reply = adapter().request({
                    'v': 1, 'id': rid, 'op': 'export', 'image': str(session['image']),
                    'format': session['format'], 'container': session['container'],
                    'name': request['name'], 'destination': request['destination']}, defs_override=session['definitions'])
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
        if snapshot:
            snapshot.cleanup()


if __name__ == '__main__':
    main()
