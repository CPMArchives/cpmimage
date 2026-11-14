# SPDX-License-Identifier: GPL-3.0-or-later
"""Physical sector inventory and mapping checks for structured images."""
from dataclasses import dataclass


class MappingError(ValueError):
    pass


@dataclass(frozen=True)
class Track:
    cylinder: int
    head: int
    mode: int
    size: int
    sectors: tuple


@dataclass(frozen=True)
class PhysicalMapping:
    """Tracks and sector IDs in independently verified RAW slot order."""

    tracks: tuple

    def verify(self, geometry):
        if len(self.tracks) != geometry['tracks'] or geometry['offset']:
            raise MappingError('Physical track count or RAW offset is unsupported.')
        addresses = set()
        for track in self.tracks:
            address = track.cylinder, track.head
            if address in addresses or not 0 <= track.cylinder <= 255 or track.head not in (0, 1):
                raise MappingError('Ambiguous or unsupported physical track address.')
            addresses.add(address)
            if (track.size != geometry['sector_size']
                    or len(track.sectors) != geometry['sectors_per_track']
                    or len(set(track.sectors)) != len(track.sectors)
                    or any(not 0 <= sector <= 255 for sector in track.sectors)
                    or track.mode not in range(6)):
                raise MappingError('Physical sectors do not match the selected RAW geometry.')


def inspect_imd(data):
    """Check the inventory before handing payload reads to LibDsk."""
    end = data.find(b'\x1a', 0, 65536)
    if not data.startswith(b'IMD ') or end < 0:
        raise MappingError('Missing or oversized IMD header.')
    header, separator, comment = data[:end].partition(b'\n')
    if b'\0' in header or b'\0' in comment:
        raise MappingError('IMD header or comment contains a NUL byte.')
    position = end + 1
    tracks = {}
    while position < len(data):
        if len(data) - position < 5:
            raise MappingError('Truncated IMD track header.')
        mode, cylinder, head, count, shift = data[position:position + 5]
        position += 5
        if mode > 5 or head not in (0, 1) or not count or shift > 6:
            raise MappingError('Unsupported IMD track geometry or sector map.')
        ids = tuple(data[position:position + count])
        position += count
        key = cylinder, head
        if key in tracks or len(set(ids)) != len(ids):
            raise MappingError('Duplicate IMD track or sector ID.')
        if len(ids) != count:
            raise MappingError('Truncated IMD sector map.')
        size = 128 << shift
        for sector in ids:
            if position >= len(data):
                raise MappingError('Truncated IMD sector record.')
            status = data[position]
            if status not in (1, 2):
                raise MappingError(f'IMD sector {cylinder}/{head}/{sector} is missing, damaged, or uses a deleted data mark.')
            position += 1 + (size if status == 1 else 1)
            if position > len(data):
                raise MappingError('Truncated IMD sector data.')
        tracks[key] = Track(cylinder, head, mode, size, ids)
    if not tracks:
        raise MappingError('IMD contains no tracks.')
    return tracks, comment if separator else b''
