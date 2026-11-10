# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch exact qualification inputs. Never install tools or accept license terms."""
import argparse,hashlib,json,os,tempfile,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[2]
def fetch(item,verify_only=False):
    dest=root/item['path'];expected=item['sha256'].lower()
    if dest.is_file() and hashlib.sha256(dest.read_bytes()).hexdigest()==expected:
        print('Verified',item['path']);return
    if verify_only:raise SystemExit('Missing or mismatched dependency: '+item['path'])
    dest.parent.mkdir(parents=True,exist_ok=True)
    fd,temp=tempfile.mkstemp(prefix=dest.name+'.',dir=dest.parent);os.close(fd)
    try:
        urllib.request.urlretrieve(item['url'],temp)
        if hashlib.sha256(Path(temp).read_bytes()).hexdigest()!=expected:raise ValueError('SHA256 mismatch: '+item['path'])
        os.replace(temp,dest);print('Downloaded and verified',item['path'])
    finally:Path(temp).unlink(missing_ok=True)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--group',choices=['backend','runtime','windows','all'],default='backend');p.add_argument('--verify-only',action='store_true');a=p.parse_args()
    for item in json.loads((root/'packaging/dependencies.json').read_text()):
        if a.group=='all' or item['group']==a.group:fetch(item,a.verify_only)
