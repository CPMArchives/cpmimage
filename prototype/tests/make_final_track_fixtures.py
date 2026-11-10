# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent IBM3740 images with one file confined to the final physical track."""
import hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'fixtures'
import sys
sys.path.insert(0,str(root/'experiments/process-comparison'));from adapters import FORMATS
skew=[0,6,12,18,24,4,10,16,22,2,8,14,20,1,7,13,19,25,5,11,17,23,3,9,15,21]
payload=bytes((i*17+(i//128)*31+93)%256 for i in range(1024))
rows=[]
for name,block,trim in [('ibm3740-final-track.raw',242,False),('ibm3740-final-track-short.raw',241,True)]:
 image=bytearray(b'\xe5'*256256);entry=bytearray(32);entry[1:12]=b'LAST    BIN';entry[15]=8;entry[16]=block
 image[6656:6688]=entry
 for n in range(8):
  track,logical=divmod(block*8+n,26);physical=(52+track*26+skew[logical])*128
  image[physical:physical+128]=payload[n*128:(n+1)*128]
 if trim:image=image[:-128]
 (out/name).write_bytes(image);rows.append(dict(file=name,size=len(image),sha256=hashlib.sha256(image).hexdigest(),file_name='00last.bin',export_sha256=hashlib.sha256(payload).hexdigest(),export_bytes=1024,block=block,format='ibm-3740',trimmed=trim,expected_before_success=False))
for fmt in ['bettercpm-default','bcm2ss','bettercpm-mm-80t-ds-data','bettercpm-ampro-little-board']:
 sec,tracks,spt,block,maxdir,boot,order=FORMATS[fmt]
 size=sec*tracks*spt;blocks=(size-boot*spt*sec)//block;last=blocks-1
 image=bytearray(b'\xe5'*size);entry=bytearray(32);entry[1:12]=b'LAST    BIN';entry[15]=block//128
 if blocks>256:entry[16:18]=last.to_bytes(2,'little')
 else:entry[16]=last
 image[boot*spt*sec:boot*spt*sec+32]=entry
 data=bytes((i*17+(i//128)*31+93)%256 for i in range(block))
 for n in range(block//sec):
  track,logical=divmod(last*(block//sec)+n,spt);physical=(boot*spt+track*spt+order[logical])*sec
  image[physical:physical+sec]=data[n*sec:(n+1)*sec]
 name='final-track-'+fmt+'.raw';(out/name).write_bytes(image)
 rows.append(dict(file=name,size=size,sha256=hashlib.sha256(image).hexdigest(),file_name='00last.bin',export_sha256=hashlib.sha256(data).hexdigest(),export_bytes=block,block=last,format=fmt,trimmed=False,expected_before_success=tracks%2==0))
(out/'final-track-manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(rows,indent=2))
