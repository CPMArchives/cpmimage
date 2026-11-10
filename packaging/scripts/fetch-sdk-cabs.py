# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch the pinned SDK cabinet set associated with the recorded MSI selection."""
import hashlib,json,concurrent.futures,urllib.request
from pathlib import Path
base=Path(__file__).resolve().parents[1];dest=base/'downloads/msvc/Installers';dest.mkdir(parents=True,exist_ok=True)
items=json.loads((base/'toolchain-inputs/sdk-cab-plan.json').read_text())
def download(item):
    p=dest/item['fileName'].split('\\')[-1]
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest().lower()!=item['sha256'].lower():urllib.request.urlretrieve(item['url'],p)
    assert hashlib.sha256(p.read_bytes()).hexdigest().lower()==item['sha256'].lower(),p
    return p.name
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for name in pool.map(download,items):print(name,flush=True)
