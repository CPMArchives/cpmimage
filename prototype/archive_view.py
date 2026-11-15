# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded archive inspection worker for the File Viewer."""
from binascii import crc_hqx
import io
import json
from pathlib import Path
import sys

# The GUI invokes Python with -I, so use only the shipped codec directory.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from archive_codecs import arc, crunch, lbr, squeeze

LIMIT = 1024 * 1024
MAX_MEMBERS = 4096


def crc_arc(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


def directory(data, name):
    upper = name.upper()
    if upper.endswith('.LBR') or data.startswith(b'\0' + b' ' * 11 + b'\0\0'):
        header = lbr.parse_entry(data)
        end = header.length * 128
        if not header.is_directory or not header.is_active or not 128 <= end <= len(data):
            raise ValueError('Invalid LBR directory')
        if end // 32 > MAX_MEMBERS:
            raise ValueError('LBR directory exceeds the viewer limit')
        members = []
        extents = []
        for pos in range(32, end, 32):
            entry = lbr.parse_entry(data[pos:pos+32])
            if not entry.is_active:
                continue
            if not entry.filename or entry.pad_count >= 128 or (
                entry.length and (entry.index * 128 < end or
                                  (entry.index + entry.length) * 128 > len(data))
            ):
                raise ValueError('Invalid LBR member extent')
            if entry.length:
                extents.append((entry.index, entry.index + entry.length))
            members.append(entry)
        extents.sort()
        if any(left[1] > right[0] for left, right in zip(extents, extents[1:])):
            raise ValueError('Overlapping LBR members')
        return 'LBR', members

    if upper.endswith(('.ARK', '.ARC')) or (len(data) >= 2 and data[0] == 0x1a and 1 <= data[1] <= 9):
        members = []
        archive = io.BytesIO(data)
        while True:
            position = archive.tell()
            if data[position:position+2] == b'\x1a\0':
                return 'ARK', members
            if position + 2 > len(data) or not 1 <= data[position + 1] <= 9:
                raise ValueError('Unknown ARK header method or missing end marker')
            member = arc.parse_header(archive)
            if member is None or not member.filename:
                raise ValueError('Missing ARK end marker or member name')
            end = member.data_offset + member.compressed_size
            if end > len(data) or len(members) >= MAX_MEMBERS:
                raise ValueError('Invalid ARK member extent or too many members')
            members.append(member)
            archive.seek(end)
    return '', []


def inspect(data, name, mode='auto', index=None, truncated=False):
    """Return directory metadata and optional original and decoded streams."""
    original = data if index is None else None
    decoded = None
    notice = ''
    if truncated:
        return {'notice': 'Preview is truncated. Open a smaller file to browse or decompress it.'}, original, None
    try:
        if index is not None:
            kind, entries = directory(data, name)
            if not 0 <= index < len(entries):
                raise ValueError('Unknown archive member')
            entry = entries[index]
            name = entry.filename
            if kind == 'LBR':
                original = lbr.read_member(io.BytesIO(data), entry)
                stored = data[entry.index * 128:(entry.index + entry.length) * 128]
                if entry.crc and entry.crc not in (crc_hqx(original, 0), crc_hqx(stored, 0)):
                    notice = 'LBR member checksum mismatch. Showing stored bytes.'
            else:
                original = data[entry.data_offset:entry.data_offset+entry.compressed_size]
                try:
                    if entry.original_size > LIMIT:
                        raise ValueError('ARK member exceeds the 1 MiB preview limit')
                    expanded = arc.decompress_member(entry, original)
                    if len(expanded) != entry.original_size or crc_arc(expanded) != entry.crc:
                        raise ValueError('ARK member size or CRC mismatch')
                    if entry.method > 2:
                        decoded = expanded
                except (ValueError, arc.ArcError) as error:
                    notice = str(error) + '. Showing stored bytes.'
        stream = decoded if decoded is not None else original
        if not notice:
            if mode == 'sq' or mode == 'auto' and stream.startswith(b'\x76\xff'):
                decoded = squeeze.unsqueeze(stream)
            elif mode == 'cr' or mode == 'auto' and stream.startswith(b'\x76\xfe'):
                decoded = crunch.uncrunch(stream)
            elif mode == 'auto' and len(name) >= 3 and name[-2:-1].upper() in ('Q', 'Z'):
                extension = name.rpartition('.')[2]
                if len(extension) == 3:
                    notice = 'Filename suggests compression, but the signature is unrecognized.'
        kind, entries = '', []
        if not notice:
            try:
                kind, entries = directory(decoded if decoded is not None else original, name)
            except (ValueError, arc.ArcError) as error:
                stream_name = 'decompressed' if decoded is not None else 'original'
                notice = f'{error}. Showing {stream_name} bytes.'
        members = []
        for entry in entries:
            size = entry.data_size if kind == 'LBR' else entry.original_size
            stored = entry.length * 128 if kind == 'LBR' else entry.compressed_size
            members.append({'name': entry.filename, 'size': size, 'stored': stored})
        return {'name': name, 'kind': kind, 'members': members, 'notice': notice}, original, decoded
    except (ValueError, IndexError, arc.ArcError, crunch.CrunchError, squeeze.SqueezeError) as error:
        return {'name': name, 'notice': str(error) + '. Showing original bytes.'}, original, None


def main():
    folder, name, mode, index, truncated = sys.argv[1:]
    folder = Path(folder)
    with (folder / 'input').open('rb') as stream:
        data = stream.read(LIMIT + 1)
    if len(data) > LIMIT:
        raise ValueError('Preview input exceeds 1 MiB')
    result, original, decoded = inspect(data, name, mode, None if index == '-' else int(index),
                                        truncated == '1')
    if index != '-':
        if original is None:
            raise ValueError('Could not read the selected archive member')
        (folder / 'original').write_bytes(original)
    if decoded is not None:
        (folder / 'decoded').write_bytes(decoded)
    print(json.dumps(result))


if __name__ == '__main__':
    main()
