# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate the four baseline synthetic fixtures without cpmtools or user images."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);out=p.parse_args().output;out.mkdir(parents=True,exist_ok=True)
skew=[0,6,12,18,24,4,10,16,22,2,8,14,20,1,7,13,19,25,5,11,17,23,3,9,15,21]
payloads=[('00binary.bin',bytes(range(256))*2),('07binary.bin',bytes(reversed(range(256)))),('15tail.bin',bytes((i*17+3)%256 for i in range(129)))]
logical=bytearray(b'\xe5'*(256256-6656));expected={}
def entry(name,data,block):
    e=bytearray(32);e[0]=int(name[:2]);stem,ext=name[2:].split('.');e[1:9]=stem.upper().ljust(8).encode();e[9:12]=ext.upper().ljust(3).encode();e[15]=len(data)//128;e[16]=block;return e
for i,(name,data) in enumerate(payloads):
    padded=data+b'\x1a'*((-len(data))%128);block=2+i
    logical[i*32:(i+1)*32]=entry(name,padded,block);logical[block*1024:block*1024+len(padded)]=padded
    expected[name]=dict(export_size=len(padded),export_sha256=hashlib.sha256(padded).hexdigest())
image=bytearray((i*29+7)%256 for i in range(6656))+bytearray(b'\xe5'*(256256-6656))
for n in range(len(logical)//128):
    track,sector=divmod(n,26);off=6656+(track*26+skew[sector])*128;image[off:off+128]=logical[n*128:(n+1)*128]
empty=bytearray(image)
for n in range(16):empty[6656+skew[n]*128:6656+(skew[n]+1)*128]=b'\xe5'*128
files={'ibm3740-independent.raw':image,'ibm3740-empty.raw':empty,'ibm3740-truncated.raw':image[:6784]}
ampro=bytearray((i*29+7)%256 for i in range(10240))+bytearray(b'\xe5'*(204800-10240))
for i,(name,data) in enumerate(payloads):
    padded=data+b'\x1a'*((-len(data))%128);ampro[10240+i*32:10240+(i+1)*32]=entry(name,padded,i+1);off=10240+(i+1)*2048;ampro[off:off+len(padded)]=padded
h=bytearray(256);h[:34]=b'MV - CPCEMU Disk-File\r\nDisk-Info\r\n';h[34:48]=b'cpmimage-probe '.ljust(14)[:14];h[48:52]=bytes([40,1,0,21]);container=bytearray(h)
for t in range(40):
    th=bytearray(256);th[:12]=b'Track-Info\r\n';th[16:24]=bytes([t,0,0,0,2,10,0x2a,0xe5])
    for sec in range(10):th[24+sec*8:32+sec*8]=bytes([t,0,sec+1,2,0,0,0,0])
    container+=th+ampro[t*5120:(t+1)*5120]
files['ampro200-independent.dsk']=container
for name,data in files.items():
    (out/name).write_bytes(data);print(name,hashlib.sha256(data).hexdigest())
(out/'expected-payloads.json').write_text(json.dumps(expected,indent=2)+'\n')
