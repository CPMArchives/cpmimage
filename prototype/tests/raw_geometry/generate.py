#!/usr/bin/env python3
# Synthetic reproduction from CPMArchives/rcpm-cpmsim, d0e08be.
# repro/cpmtools-raw-geometry/generate.py; preserved for reproducibility.
"""Generate synthetic CP/M raw images demonstrating geometry-probe contamination."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def directory_entry(user, name, extension, block, block_size, length):
    entry = bytearray(32)
    entry[0] = user
    entry[1:12] = (name.ljust(8) + extension.ljust(3)).encode('ascii')
    entry[15] = length // 128
    for index in range((length + block_size - 1) // block_size):
        entry[16 + index * 2:18 + index * 2] = (block + index).to_bytes(2, 'little')
    return entry


def pattern(offset, size):
    return b''.join(struct.pack('<II', sector, sector ^ 0xffffffff) * 16
                    for sector in range(offset // 128, (offset + size) // 128))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = []
    cases = (
        ('four-mb-failing', 'z80pack-hd', 4177920, 2048, 1024, 17, 'VOLUME', False),
        ('four-mb-control', 'z80pack-hd', 4177920, 2048, 1024, 17, 'TOLUME', True),
        ('large-failing', 'z80pack-hdb', 536870912, 16384, 8192, 257, 'VOLUME', False),
    )
    for name, geometry, size, block_size, entries, block, volume_name, unpatched_pass in cases:
        image = args.output / (name + '.dsk')
        # The 512 MiB image is sparse after a 12 MiB diagnostic prefix.
        prefix = min(size, 12 * 1024**2)
        with image.open('xb') as stream:
            for offset in range(0, prefix, 16384):
                stream.write(pattern(offset, min(16384, prefix - offset)))
            stream.truncate(size)
            stream.seek(0)
            directory = bytearray(b'\xe5' * (entries * 32))
            directory[:32] = directory_entry(0, volume_name, 'TXT',
                                             entries * 32 // block_size, block_size, 128)
            directory[32:64] = directory_entry(1, 'PATTERN', 'BIN', block, block_size, 16384)
            stream.write(directory)
        expected = args.output / (name + '.expected.bin')
        expected.write_bytes(pattern(block * block_size, 16384))
        manifest.append({'image': image.name, 'format': geometry,
                         'filespec': '1:PATTERN.BIN', 'expected': expected.name,
                         'unpatched_matches': unpatched_pass,
                         'image_bytes': size,
                         'expected_sha256': hashlib.sha256(expected.read_bytes()).hexdigest()})
    (args.output / 'cases.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (args.output / 'diskdefs').write_text('''diskdef z80pack-hd
  seclen 128
  tracks 255
  sectrk 128
  blocksize 2048
  maxdir 1024
  skew 0
  boottrk 0
  os 2.2
end

diskdef z80pack-hdb
  seclen 128
  tracks 256
  sectrk 16384
  blocksize 16384
  maxdir 8192
  skew 0
  boottrk 0
  os 2.2
end
''')
    with (args.output / 'SHA256SUMS').open('w') as sums:
        for file in sorted(args.output.iterdir()):
            if file.name == 'SHA256SUMS':
                continue
            digest = hashlib.sha256()
            with file.open('rb') as stream:
                for chunk in iter(lambda: stream.read(1024**2), b''):
                    digest.update(chunk)
            sums.write(f'{digest.hexdigest()}  {file.name}\n')
    print(f'Generated {len(cases)} synthetic images in {args.output}')


if __name__ == '__main__':
    main()
