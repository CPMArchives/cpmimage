# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage the editor backend and locked CPython 3.13 validator wheels."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[2]
EDITOR_FILES = (
    'prototype/disk_format.py',
    'prototype/disk_format_store.py',
    'prototype/requirements-disk-format.txt',
    'prototype/schemas/disk-format-definition.schema.json',
    'prototype/schemas/mapping-sidecar.schema.json',
)


def stage_editor(destination):
    for name in EDITOR_FILES:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)


def install_validator(runtime, target):
    dependencies = json.loads((ROOT / 'packaging/dependencies.json').read_text(encoding='utf-8'))
    wheels = [item for item in dependencies
              if item['group'] == 'validator' and item['target'] in ('all', target)]
    if len(wheels) != 5 or sum(item['target'] == target for item in wheels) != 1:
        raise ValueError(f'Incomplete validator wheel lock for {target}')
    windows = target.startswith('windows-')
    # Upstream and Debian CPython agree on the private standard-library
    # directory, even when their system package search paths differ.
    packages = runtime / ('site-packages' if windows else 'lib/python3.13')
    packages.mkdir(parents=True, exist_ok=True)
    for item in wheels:
        path = ROOT / item['path']
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError(f'Fetch verified validator wheels first: {item["path"]}')
        # These locked wheels install directly from their archive root. Keep
        # metadata and license files alongside the importable modules.
        with zipfile.ZipFile(path) as wheel:
            for member in wheel.infolist():
                name = PurePosixPath(member.filename)
                if (name.is_absolute() or '..' in name.parts or '\\' in member.filename or
                        any(part.endswith('.data') for part in name.parts)):
                    raise ValueError(f'Unsupported wheel layout: {member.filename}')
                wheel.extract(member, packages)
    if windows:
        config = runtime / 'python313._pth'
        lines = config.read_text(encoding='utf-8-sig').splitlines()
        if 'site-packages' not in lines:
            # Embedded Python keeps its explicit search list under -I. No
            # import site or environment-controlled search path is needed.
            lines.append('site-packages')
        config.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return packages
