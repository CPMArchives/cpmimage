# SPDX-License-Identifier: GPL-3.0-or-later
# Based on avwohl/80un; see NOTICE.md for provenance and modifications.
"""
Squeeze decompression for CP/M files.

Squeeze uses Huffman coding combined with run-length encoding (RLE).
It was devised by Richard Greenlaw circa 1981.

File format:
- Magic: 0x76 0xFF
- Original filename: null-terminated string
- Checksum: 2 bytes (little-endian, sum of decoded bytes)
- Huffman tree node count: 2 bytes (little-endian, signed)
- Huffman tree: array of node pairs (2 bytes each, little-endian signed)
- Compressed data: bit stream

Huffman tree:
- Each node has two children (left and right)
- Positive values are child node indices
- Negative values are leaf values (-1 = value 0, -2 = value 1, etc.)
- Special value -1 (as leaf, not -2 for byte 0) can mean EOF

RLE encoding (RLE90):
- 0x90 is the escape byte
- 0x90 0x00 = a literal 0x90; the previous byte is left alone
- 0x90 N (N > 0) = the previous byte occurs N times in TOTAL, so N-1 further
  copies are emitted - see decode_rle below
"""

from __future__ import annotations

import struct
from typing import Iterator

SQUEEZE_MAGIC = 0x76FF
RLE_MARKER = 0x90
EOF_VALUE = 256  # Special EOF marker in Huffman tree


OUTPUT_LIMIT = 1024 * 1024


class SqueezeError(Exception):
    """Error during squeeze decompression."""


class BitReader:
    """Read individual bits from a byte stream, LSB first."""

    def __init__(self, data: bytes, offset: int = 0):
        self.data = data
        self.pos = offset
        self.bit_pos = 8  # Force initial read (like USQ's bpos=99)
        self.current_byte = 0

    def read_bit(self) -> int:
        """Read a single bit LSB-first, returns 0 or 1."""
        if self.bit_pos >= 8:
            if self.pos >= len(self.data):
                raise SqueezeError("Unexpected end of data")
            self.current_byte = self.data[self.pos]
            self.pos += 1
            self.bit_pos = 0

        bit = (self.current_byte >> self.bit_pos) & 1
        self.bit_pos += 1
        return bit


class HuffmanTree:
    """Huffman tree for squeeze decompression."""

    def __init__(self, nodes: list[tuple[int, int]]):
        """
        Initialize with node array.

        Args:
            nodes: List of (left, right) child indices.
                   Positive = node index, negative = -(value + 1)
        """
        self.nodes = nodes

    def decode_symbol(self, bits: BitReader) -> int:
        """
        Decode one symbol from the bit stream.

        Returns:
            The decoded byte value (0-255) or EOF_VALUE (256)
        """
        if not self.nodes:
            raise SqueezeError("Empty Huffman tree")

        node_idx = 0

        for _ in self.nodes:
            if node_idx < 0 or node_idx >= len(self.nodes):
                raise SqueezeError(f"Invalid node index: {node_idx}")

            left, right = self.nodes[node_idx]
            bit = bits.read_bit()

            child = right if bit else left

            if child < 0:
                # Leaf node: value is -(child + 1)
                value = -(child + 1)
                return value

            # Internal node: continue traversal
            node_idx = child
        raise SqueezeError("Cyclic Huffman tree")


def decode_rle(data: Iterator[int]) -> bytes:
    """
    Decode RLE90-encoded data.

    Args:
        data: Iterator of byte values

    Returns:
        Decoded bytes
    """
    result = bytearray()
    prev_byte = 0

    for byte in data:
        if byte == RLE_MARKER:
            try:
                count = next(data)
            except StopIteration:
                # A marker with no count after it carries no data, so it is
                # dropped - the CP/M decoders only arm the escape flag and then
                # run out of input.
                raise SqueezeError("Missing RLE count")

            if count == 0:
                # Literal 0x90; the previous byte is left alone
                result.append(RLE_MARKER)
                if len(result) > OUTPUT_LIMIT:
                    raise SqueezeError("Preview output exceeds 1 MiB")
            else:
                # The previous byte occurs `count` times in total, and it was
                # already written when it was read as a literal, so emit
                # count - 1 further copies.  Confirmed against the 16-bit
                # checksum in the squeeze header.
                result.extend([prev_byte] * (count - 1))
                if len(result) > OUTPUT_LIMIT:
                    raise SqueezeError("Preview output exceeds 1 MiB")
        else:
            result.append(byte)
            if len(result) > OUTPUT_LIMIT:
                raise SqueezeError("Preview output exceeds 1 MiB")
            prev_byte = byte

    return bytes(result)


def unsqueeze(data: bytes) -> bytes:
    """
    Decompress squeezed data.

    Args:
        data: Squeezed file data (including magic header)

    Returns:
        Decompressed data

    Raises:
        SqueezeError: If decompression fails
    """
    if len(data) < 4:
        raise SqueezeError("Data too short")

    # Check magic
    magic = (data[0] << 8) | data[1]
    if magic != SQUEEZE_MAGIC:
        raise SqueezeError(f"Invalid magic: 0x{magic:04X}, expected 0x{SQUEEZE_MAGIC:04X}")

    pos = 2

    # Read checksum (comes BEFORE filename per original USQ format)
    if pos + 2 > len(data):
        raise SqueezeError("Data too short for checksum")
    checksum = struct.unpack('<H', data[pos:pos+2])[0]
    pos += 2

    # Skip original filename (null-terminated)
    while pos < len(data) and data[pos] != 0:
        pos += 1
    pos += 1  # Skip null terminator

    if pos + 2 > len(data):
        raise SqueezeError("Data too short for header")

    # Read node count (signed 16-bit)
    node_count = struct.unpack('<h', data[pos:pos+2])[0]
    pos += 2

    # A node for each of the 257 symbols (the bytes and the end code) but one,
    # so 256 at most.  USQ refuses more, and so do 80un and ARC method 4 here.
    if node_count < 0 or node_count > 256:
        raise SqueezeError(f"Invalid node count: {node_count}")

    # Read Huffman tree nodes
    nodes = []
    for _ in range(node_count):
        if pos + 4 > len(data):
            raise SqueezeError("Data too short for Huffman tree")
        left, right = struct.unpack('<hh', data[pos:pos+4])
        nodes.append((left, right))
        pos += 4

    # Build tree and decode
    tree = HuffmanTree(nodes)
    bits = BitReader(data, pos)

    # Decode Huffman symbols
    def symbols():
        while nodes:
            symbol = tree.decode_symbol(bits)
            if symbol == EOF_VALUE:
                return
            if symbol > 255:
                raise SqueezeError(f"Invalid symbol: {symbol}")
            yield symbol

    result = decode_rle(symbols())
    if sum(result) & 0xffff != checksum:
        raise SqueezeError("Squeeze checksum mismatch")

    return result
