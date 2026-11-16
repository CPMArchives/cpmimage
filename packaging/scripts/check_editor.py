# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise disk-format editor workflows through the isolated packaged runtime."""
import hashlib
import json
from pathlib import Path
import subprocess

from editor_bundle import EDITOR_FILES


RAW = '''diskdef synthetic-256k
  seclen 128
  tracks 64
  sectrk 32
  blocksize 1024
  maxdir 128
  boottrk 0
end
'''

IMD = '''diskdef example-imd-8k
  seclen 512
  tracks 4
  sectrk 4
  blocksize 1024
  maxdir 32
  bootsec 4
  os 2.2
end
'''

TRACKS = [[0, 0], [0, 1], [1, 0], [1, 1]]
FIELDS = dict(mode=5, size=512, sectors=[1, 3, 2, 4])
MARKER = b'Physical sector ID 2'.ljust(128, b'!')


def imd_fixture(mismatch=False):
    image = bytearray(b'IMD 1.18: packaged editor check\r\n\x1a')
    entry = b'\x00PROBE   BIN' + bytes([0, 0, 0, 1, 1]) + bytes(15)
    for cylinder, head in TRACKS:
        directory_track = (cylinder, head) == (0, 1)
        ids = [1, 2, 3, 5 if directory_track and mismatch else 4]
        image.extend(bytes([5, cylinder, head, 4, 2, *ids]))
        for sector in ids:
            prefix = b''
            if directory_track and sector == 1:
                prefix = entry
            elif directory_track and sector == 2:
                prefix = MARKER
            image.extend(bytes([1]) + prefix.ljust(512, b'\xe5'))
    return bytes(image)


def verify_runtime(root, runtime, env, directory):
    for relative in EDITOR_FILES:
        assert (root / relative).is_file(), relative
    script = '''
import importlib.metadata
import json
from pathlib import Path
import sys
import jsonschema
import rpds

runtime = Path(sys.executable).resolve().parents[0 if sys.platform == "win32" else 1]
for module in (jsonschema, rpds):
    assert Path(module.__file__).resolve().is_relative_to(runtime), module.__file__
for path in (Path(sys.argv[1]) / "prototype/schemas").glob("*.schema.json"):
    jsonschema.Draft202012Validator.check_schema(json.loads(path.read_bytes()))
print(json.dumps({name: importlib.metadata.version(name) for name in
                  ("jsonschema", "jsonschema-specifications", "attrs", "referencing", "rpds-py")}))
'''
    output = subprocess.check_output([str(runtime), '-I', '-B', '-c', script, str(root)],
                                     env=env, cwd=directory, timeout=15)
    versions = json.loads(output)
    expected = (root / 'prototype/requirements-disk-format.txt').read_text().strip()
    assert expected == 'jsonschema==' + versions['jsonschema'], versions
    return versions


def check_editor(request, directory, helper, env, validator_versions):
    directory.mkdir()

    def success(op, **fields):
        reply = request(op, **fields)
        assert reply['ok'], reply
        return reply['result']

    def assert_intact(inputs):
        for path, data in inputs.items():
            assert path.read_bytes() == data, path

    records = []
    for definition, boot, directory_blocks in ((RAW, 0, 4), (IMD, 4, 1)):
        record = success('definition_import', text=definition)['record']
        validated = success('definition_validate', record=record)
        geometry = validated['geometry']
        assert validated['diskdefs'] == definition
        assert (geometry['boot_sectors'], geometry['directory_blocks'], geometry['extent_factor']) == (
            boot, directory_blocks, 1), geometry
        assert success('definition_save', record=record)['definition_sha256'] == validated['definition_sha256']
        records.append(record)
    assert {item['record_id'] for item in success('definition_list')['records']} == {
        record['record_id'] for record in records}
    raw, imd = records
    raw_image = directory / 'synthetic.raw'
    raw_image.write_bytes(b'\xe5' * 262144)
    success('open', image=str(raw_image), format=raw['identifier'], container='raw')

    profile = dict(track_order=TRACKS, default=FIELDS)
    legacy_metadata = dict(profile, note='Preserve legacy metadata')
    legacy_overrides = dict(track_order=TRACKS, default={},
                            tracks={f'{c}/{h}': FIELDS for c, h in TRACKS})
    image = directory / 'example ü.imd'
    sidecar = image.with_name(image.name + '.mapping.json')
    open_args = dict(image=str(image), format=imd['identifier'], container='imd')
    cases = []
    for name, mapping in (('standard', profile), ('legacy_metadata', legacy_metadata),
                          ('legacy_complete_overrides', legacy_overrides)):
        inputs = {image: imd_fixture(), sidecar: (json.dumps(mapping, indent=2) + '\r\n').encode()}
        for path, data in inputs.items():
            path.write_bytes(data)
        opened = success('open', **open_args)
        assert [entry['name'] for entry in opened['entries']] == ['00PROBE.BIN'], opened
        export = directory / (name + '.bin')
        success('export', session=opened['session'], name='00PROBE.BIN', destination=str(export))
        assert export.read_bytes() == MARKER
        strict_rejected = name != 'standard'
        if strict_rejected:
            rejected = request('mapping_write', record=imd, image=str(image), profile=mapping,
                               confirmed=True, mapping_sha256=hashlib.sha256(inputs[sidecar]).hexdigest())
            assert not rejected['ok'] and rejected['error'] == 'invalid_mapping', rejected
        assert_intact(inputs)
        cases.append(dict(case=name, export_sha256=hashlib.sha256(MARKER).hexdigest(),
                          inputs_unchanged=True, strict_write_rejected=strict_rejected))

    bad_definition = IMD.replace('seclen 512', 'seclen 384').encode()
    diskdefs = directory / 'diskdefs'
    diskdefs.write_bytes(bad_definition)
    parsed = subprocess.run([str(helper), 'describe', imd['identifier']], capture_output=True,
                            cwd=directory, env=env, timeout=15)
    assert parsed.returncode == 1 and b'invalid disk definition' in parsed.stderr, parsed
    record = success('definition_import', text=bad_definition.decode())['record']
    rejected = request('definition_validate', record=record)
    assert not rejected['ok'] and rejected['error'] == 'invalid_definition', rejected
    assert_intact({diskdefs: bad_definition})
    cases.append(dict(case='indivisible_block', layer='native_helper', error=rejected['error'],
                      inputs_unchanged=True))

    good_definition = IMD.encode()
    diskdefs.write_bytes(good_definition)
    for name, layer, contents, mismatched, expected in (
        ('inventory_mismatch', 'mapping_verifier', json.dumps(profile).encode(), True,
         'Container sectors differ on track 0/1'),
        ('malformed_sidecar', 'json_decoder', b'{"track_order": [', False, 'Expecting value'),
    ):
        inputs = {diskdefs: good_definition, image: imd_fixture(mismatched), sidecar: contents}
        image.write_bytes(inputs[image])
        sidecar.write_bytes(contents)
        rejected = request('open', **open_args)
        assert not rejected['ok'] and rejected['error'] == 'physical_mapping', rejected
        assert expected in rejected['message'], rejected
        assert_intact(inputs)
        cases.append(dict(case=name, layer=layer, error=rejected['error'], inputs_unchanged=True))
    return dict(validator=validator_versions, cases=cases)
