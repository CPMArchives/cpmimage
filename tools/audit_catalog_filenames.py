# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit every metadata-bearing filename row in SIG/M's fixed-column CATALOG.ALL."""
import argparse,collections,hashlib,json,re
from pathlib import Path

def audit(data):
    text=data.decode('utf-8');lines=text.splitlines();rows=[];unparsed=[];volume=None;volumes=set()
    for lineno,line in enumerate(lines,1):
        heading=re.match(r'^SIG/M Volume\s+(\d+)',line,re.I)
        if heading:volume=int(heading[1]);volumes.add(volume)
        metadata=re.match(r'^\s+(\d+)\s+(\d+)[kK]\s+(\d+)\s+([0-9A-Fa-f]{4})(?:\s|$)',line[19:])
        candidate=re.match(r'^\s*\d+\.\d+\s',line) or re.match(r'^\s{7}\S.*\s+\d+\s+\d+[kK]\s+\d+\s+[0-9A-Fa-f]{4}',line)
        if metadata and len(line)>18 and line[15]=='.':
            stem=line[7:15].rstrip();ext=line[16:19].rstrip();name=stem+('.'+ext if ext else '')
            rows.append(dict(volume=volume,line=lineno,name=name,stem=stem,extension=ext))
        elif candidate:unparsed.append(dict(line=lineno,text=line))
    if unparsed:raise ValueError('Unparsed file rows: '+repr(unparsed))
    punctuation={}
    for row in rows:
        for ch in sorted(set(row['stem']+row['extension'])):
            if not ch.isalnum():punctuation.setdefault(ch,[]).append({k:row[k] for k in ['volume','line','name']})
    # Classic DOS FAT short names, not Windows/VFAT long filenames.
    dos_forbidden=set('"*+,/:;<=>?[\\]|')
    windows_forbidden=set('<>:"/\\|?*')
    violations={}
    for os_name,bad in [('macOS',{'/','\0'}),('Linux',{'/','\0'}),('Windows',windows_forbidden|set(map(chr,range(32)))),('DOS_8_3',dos_forbidden|set(map(chr,range(33))))]:
        violations[os_name]=[{k:row[k] for k in ['volume','line','name']} for row in rows if any(ch in bad for ch in row['stem']+row['extension'])]
    reserved=[{k:row[k] for k in ['volume','line','name']} for row in rows if re.fullmatch(r'CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]',row['stem'],re.I)]
    blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
    return dict(source_url='https://github.com/CPMArchives/SIGM/blob/main/-CATALOG.ALL',git_blob_sha1=blob,sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),lines=len(lines),volumes=len(volumes),filename_entries=len(rows),unique_filenames=len({x['name'] for x in rows}),unparsed_rows=unparsed,punctuation={k:dict(entries=len(v),unique_names=len({x['name'] for x in v}),occurrences=v) for k,v in sorted(punctuation.items())},violations=violations,dos_windows_reserved_names=reserved)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('catalogue',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--expected-blob');a=p.parse_args()
    report=audit(a.catalogue.read_bytes())
    if a.expected_blob and report['git_blob_sha1']!=a.expected_blob:raise SystemExit('Catalogue differs from the requested GitHub snapshot')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['punctuation','violations']},indent=2))
    print('Punctuation:',{k:v['entries'] for k,v in report['punctuation'].items()})
