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


def prepare_read_only_image(data, fmt, container, backend):
    """Accept a trimmed IBM3740 tail only when every allocated sector is present.

    Never use this normalization for mutation. The source/hash stay unchanged;
    only the retained read-only snapshot receives empty, unallocated sectors.
    """
    if fmt != 'ibm-3740' or container != 'raw' or len(data) == 256256:
        backend.validate(data, fmt, container, read_only=True)
        return data, False
    if not data or len(data) > 256256 or len(data) % 128:
        raise backend.Failure('image_size',
            'IBM 3740 raw images must contain whole 128-byte sectors and be no larger than 256,256 bytes.')
    sec, tracks, spt, block, maxdir, boot, skew = backend.FORMATS[fmt]
    def physical_end(logical_sector):
        track, sector = divmod(logical_sector, spt)
        return (boot * spt + track * spt + skew[sector] + 1) * sec
    directory_sectors = (maxdir * 32 + sec - 1) // sec
    if any(physical_end(n) > len(data) for n in range(directory_sectors)):
        raise backend.Failure('image_truncated', 'The image is missing directory sectors; it cannot be opened safely.')
    padded = data + b'\xe5' * (sec * tracks * spt - len(data))
    backend.validate(padded, fmt, container, read_only=True)
    directory = b''.join(padded[physical_end(n)-sec:physical_end(n)]
                         for n in range(directory_sectors))
    for offset in range(0, maxdir * 32, 32):
        entry = directory[offset:offset+32]
        if entry[0] == 0xe5:
            continue
        for allocated in entry[16:32]:
            if allocated and any(physical_end(allocated * (block // sec) + n) > len(data)
                                 for n in range(block // sec)):
                raise backend.Failure('image_truncated',
                    'The image is missing allocated file sectors; no missing file data will be invented.')
    return padded, True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--helper', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.root / 'experiments/process-comparison'))
    import adapters as backend
    backend.HELPER = args.helper.resolve()
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
            if not stat.S_ISREG(info.st_mode) or info.st_size > 16777216:
                raise backend.Failure('input_limit', 'Choose a regular disk-image file no larger than 16 MiB.')
            data = stream.read(16777217)
        if len(data) > 16777216:
            raise backend.Failure('input_limit', 'Image exceeds the 16 MiB prototype limit.')
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
                prepared, trimmed = prepare_read_only_image(data, request['format'], request['container'], backend)
                snapshot = tempfile.TemporaryDirectory(prefix='cpmimage-open-')
                image = Path(snapshot.name) / ('image.' + request['container'])
                image.write_bytes(prepared)
                reply = adapter().request({
                    'v': 1, 'id': rid, 'op': 'list', 'image': str(image),
                    'format': request['format'], 'container': request['container']})
                if not reply['ok']:
                    respond(reply)
                    return
                if stop.is_set():
                    raise backend.Failure('cancelled')
                token = str(uuid.uuid4())
                fingerprint = hashlib.sha256(data).hexdigest()
                session = dict(token=token, source=source, image=image, sha=fingerprint,
                               format=request['format'], container=request['container'])
                reply['result'].update(session=token, source_sha256=fingerprint, unused_tail_filled=trimmed)
            else:
                if not session or request.get('session') != session['token']:
                    raise backend.Failure('stale_session', 'Open the image again.')
                if hashlib.sha256(read_image(session['source'])).hexdigest() != session['sha']:
                    raise backend.Failure('source_changed', 'The source changed. Open it again.')
                reply = adapter().request({
                    'v': 1, 'id': rid, 'op': 'export', 'image': str(session['image']),
                    'format': session['format'], 'container': session['container'],
                    'name': request['name'], 'destination': request['destination']})
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
