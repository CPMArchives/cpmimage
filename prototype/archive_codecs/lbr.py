# SPDX-License-Identifier: GPL-3.0-or-later
# Based on avwohl/80un; see NOTICE.md for provenance and modifications.
"""
LBR (Library) archive format support.

LBR is an archive format (like tar) used on CP/M. It stores files
without compression, but individual members are often squeezed or crunched.

Format:
- Files are organized into 128-byte sectors
- First sector(s) contain the directory
- Each directory entry is 32 bytes
- First entry is the directory itself (name = spaces)

Directory entry format (32 bytes):
  Offset  Size  Description
  0       1     Status: 0x00=active, 0xFE=deleted, 0xFF=unused
  1       8     Filename (space-padded, high bit may have attributes)
  9       3     Extension (space-padded, high bit may have attributes)
  12      2     Index: first sector of member (little-endian)
  14      2     Length: size in sectors (little-endian)
  16      2     CRC-16 (XMODEM style)
  18      2     Creation date (days since Dec 31, 1977)
  20      2     Last change date
  22      2     Creation time (DOS format)
  24      2     Last change time
  26      1     Pad count: trailing pad bytes in final sector (0-127)
  27      5     Reserved (zeros)
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import BinaryIO

SECTOR_SIZE = 128
ENTRY_SIZE = 32

STATUS_ACTIVE = 0x00
STATUS_DELETED = 0xFE
STATUS_UNUSED = 0xFF


@dataclass
class LbrEntry:
    """A single entry in an LBR archive."""
    status: int
    name: str
    ext: str
    index: int  # First sector
    length: int  # Length in sectors
    crc: int
    creation_date: int
    change_date: int
    creation_time: int
    change_time: int
    pad_count: int

    @property
    def filename(self) -> str:
        """Full filename with extension."""
        if self.ext.strip():
            return f"{self.name.strip()}.{self.ext.strip()}"
        return self.name.strip()

    @property
    def is_active(self) -> bool:
        return self.status == STATUS_ACTIVE

    @property
    def is_deleted(self) -> bool:
        return self.status == STATUS_DELETED

    @property
    def is_directory(self) -> bool:
        """Check if this is the directory entry itself."""
        return self.name.strip() == '' and self.ext.strip() == '' and self.index == 0

    @property
    def data_size(self) -> int:
        """Actual data size in bytes (accounting for padding)."""
        if self.length == 0:
            return 0
        return (self.length * SECTOR_SIZE) - self.pad_count


def parse_entry(data: bytes) -> LbrEntry:
    """Parse a 32-byte directory entry."""
    if len(data) < ENTRY_SIZE:
        raise ValueError(f"Entry too short: {len(data)} bytes")

    status = data[0]

    # Extract name and extension, masking high bits (CP/M attributes)
    name = ''.join(chr(b & 0x7F) for b in data[1:9])
    ext = ''.join(chr(b & 0x7F) for b in data[9:12])

    # Parse numeric fields (little-endian)
    index, length, crc = struct.unpack('<HHH', data[12:18])
    creation_date, change_date = struct.unpack('<HH', data[18:22])
    creation_time, change_time = struct.unpack('<HH', data[22:26])
    pad_count = data[26]

    return LbrEntry(
        status=status,
        name=name,
        ext=ext,
        index=index,
        length=length,
        crc=crc,
        creation_date=creation_date,
        change_date=change_date,
        creation_time=creation_time,
        change_time=change_time,
        pad_count=pad_count,
    )


def read_member(f: BinaryIO, entry: LbrEntry) -> bytes:
    """
    Read a member's data from the archive.

    Args:
        f: Open file handle
        entry: The directory entry for the member

    Returns:
        The member's data
    """
    f.seek(entry.index * SECTOR_SIZE)
    data = f.read(entry.length * SECTOR_SIZE)

    # Trim padding if pad_count is set
    if entry.pad_count > 0 and len(data) >= entry.pad_count:
        data = data[:-entry.pad_count]

    return data
