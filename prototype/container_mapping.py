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

    @classmethod
    def from_profile(cls, profile, geometry):
        """Expand explicit RAW track order and optional per-track overrides."""
        try:
            order = profile['track_order']
            defaults = profile['default']
            overrides = profile.get('tracks', {})
            if len(order) != geometry['tracks']:
                raise MappingError('Profile does not cover every RAW track.')
            tracks = []
            used = set()
            for cylinder, head in order:
                key = f'{cylinder}/{head}'
                fields = {**defaults, **overrides.get(key, {})}
                used.add(key)
                tracks.append(Track(cylinder, head, fields['mode'],
                                    fields['size'], tuple(fields['sectors'])))
            if set(overrides) - used:
                raise MappingError('Profile contains an override for an unmapped track.')
            mapping = cls(tuple(tracks))
            mapping.verify(geometry)
            return mapping
        except (KeyError, TypeError, ValueError) as error:
            if isinstance(error, MappingError):
                raise
            raise MappingError('Invalid physical mapping profile.') from error

    def verify_inventory(self, inventory):
        """Confirm a source has precisely the addresses and properties expected."""
        if len(inventory) != len(self.tracks):
            raise MappingError('Container track inventory differs from the mapping.')
        for track in self.tracks:
            observed = inventory.get((track.cylinder, track.head))
            if (not observed or observed.size != track.size or observed.mode != track.mode
                    or set(observed.sectors) != set(track.sectors)):
                raise MappingError(f'Container sectors differ on track {track.cylinder}/{track.head}.')

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


def transfer_dmk(data, mapping, raw=None):
    """Read or update ordinary MFM DMK sectors using a verified track mapping.

    Writes retain the source track representation and update only payload CRCs.
    """
    from binascii import crc_hqx

    if len(data) < 16 or data[0] not in (0, 255):
        raise MappingError('Invalid DMK header.')
    cylinders = data[1]
    length = int.from_bytes(data[2:4], 'little')
    heads = 1 if data[4] & 16 else 2
    if (not cylinders or length <= 128 or len(data) != 16 + cylinders * heads * length
            or len(mapping.tracks) != cylinders * heads or data[4] & ~16 or any(data[5:16])):
        raise MappingError('Unsupported DMK geometry or header options.')
    if raw is not None and data[0]:
        raise MappingError('DMK source is write protected.')
    output = bytearray(data) if raw is not None else bytearray()
    cursor = 0
    for track in mapping.tracks:
        if track.cylinder >= cylinders or track.head >= heads or track.mode < 3:
            raise MappingError('DMK requires a verified MFM track mapping.')
        base = 16 + (track.cylinder * heads + track.head) * length
        limit = base + length
        pointers = []
        for index in range(base, base + 128, 2):
            value = int.from_bytes(data[index:index + 2], 'little')
            if not value:
                break
            offset = value & 0x3fff
            if value & 0xc000 != 0x8000 or not 128 <= offset < length:
                raise MappingError('Unsupported DMK density or ID pointer.')
            pointers.append(base + offset)
        if len(pointers) != len(track.sectors) or pointers != sorted(set(pointers)):
            raise MappingError('Missing or ambiguous DMK ID pointers.')
        slots = {}
        for position, end in zip(pointers, pointers[1:] + [limit]):
            if position + 7 > end or data[position:position + 3] != bytes((0xfe, track.cylinder, track.head)):
                raise MappingError('DMK sector address differs from the mapping.')
            _, _, _, sector, shift = data[position:position + 5]
            if (sector in slots or shift > 6 or 128 << shift != track.size
                    or crc_hqx(b'\xa1' * 3 + data[position:position + 7], 0xffff)):
                raise MappingError('Ambiguous or damaged DMK sector ID.')
            mark = data.find(b'\xa1\xa1\xa1\xfb', position + 7, end)
            deleted = data.find(b'\xa1\xa1\xa1\xf8', position + 7, end)
            if mark < 0 or 0 <= deleted < mark or mark + 6 + track.size > end:
                raise MappingError('Missing or unsupported DMK data mark.')
            start = mark + 4
            finish = start + track.size
            if crc_hqx(data[mark:finish + 2], 0xffff):
                raise MappingError('Damaged DMK data CRC.')
            slots[sector] = (start, finish)
        if set(slots) != set(track.sectors):
            raise MappingError('DMK sector IDs differ from the mapping.')
        for sector in track.sectors:
            start, finish = slots[sector]
            if raw is None:
                output.extend(data[start:finish])
            else:
                chunk = raw[cursor:cursor + track.size]
                if len(chunk) != track.size:
                    raise MappingError('RAW payload is shorter than the DMK mapping.')
                output[start:finish] = chunk
                output[finish:finish + 2] = crc_hqx(output[start - 4:finish], 0xffff).to_bytes(2, 'big')
                cursor += track.size
    if raw is not None and cursor != len(raw):
        raise MappingError('RAW payload is larger than the DMK mapping.')
    return bytes(output)
