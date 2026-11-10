# SPDX-License-Identifier: GPL-3.0-or-later
"""Experimental parent contract shared by CLI and isolated internal-API child.
Known layouts only; POSIX process supervision; no source replacement or architecture choice.
"""
import hashlib,json,os,re,selectors,shutil,signal,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
TOOLS=ROOT/'vendor/libdsk-build/cpmtools-2.23'
HELPER=Path(__file__).with_name('helper')
DEFS=(ROOT/'fixtures/diskdefs').read_text()
# Explicitly vetted fixture layouts. User diskdefs discovery is deliberately not implemented.
FORMATS={'ibm-3740':(128,77,26,1024,64,2,[0,6,12,18,24,4,10,16,22,2,8,14,20,1,7,13,19,25,5,11,17,23,3,9,15,21]),
 'bettercpm-default':(256,80,18,2048,64,6,[0,4,8,12,16,2,6,10,14,1,5,9,13,17,3,7,11,15]),
 'bcm2ss':(512,77,16,2048,128,4,list(range(16))),
 'bettercpm-mm-80t-ds-data':(512,160,10,2048,128,0,[0,2,4,6,8,1,3,5,7,9]),
 'bettercpm-ampro-little-board':(512,40,10,2048,64,2,list(range(10)))}
NAME=re.compile(r'^(0[0-9]|1[0-5])[a-z0-9!#$%&\x27()@^_`{}~-]{1,8}(?:\.[a-z0-9!#$%&\x27()@^_`{}~-]{1,3})?$')
# CP/M '/' is an opaque image filename character, never a host path.
READ_ONLY_NAME=re.compile('^(0[0-9]|1[0-5])[a-z0-9!#$%&\'()@^_`{}~+/:\\\\|"-]{1,8}(?:\\.[a-z0-9!#$%&\'()@^_`{}~+/:\\\\|"-]{1,3})?$',re.IGNORECASE)
class Failure(Exception):
 def __init__(self,code,message=''):self.code=code;self.message=message;super().__init__(code+': '+message)
def digest(b):return hashlib.sha256(b).hexdigest()
def filename(n,read_only=False):
 if not isinstance(n,str):raise Failure('invalid_name')
 if not n.isascii():raise Failure('invalid_name')
 n=n.upper() if read_only else n.lower()
 if not (READ_ONLY_NAME if read_only else NAME).fullmatch(n):raise Failure('invalid_name','supported scope: user 0-15 and strict CP/M 8.3')
 return n
def validate(data,fmt,container,read_only=False):
 if fmt not in FORMATS:raise Failure('unsupported_format')
 sec,tracks,spt,block,maxdir,boot,skew=FORMATS[fmt]
 isdsk=data.startswith(b'MV - CPCEMU Disk-File\r\nDisk-Info\r\n')
 if container=='raw':
  if isdsk:raise Failure('container_mismatch')
  if len(data)!=sec*tracks*spt:raise Failure('image_size')
  raw=data
 elif container=='dsk':
  # This experiment supports only the proven standard Ampro DSK combination.
  if fmt!='bettercpm-ampro-little-board':raise Failure('unsupported_combination')
  if not isdsk or len(data)<256 or data[48:50]!=bytes([40,1]):raise Failure('container_header')
  ts=int.from_bytes(data[50:52],'little')
  if ts!=256+sec*spt or len(data)!=256+40*ts:raise Failure('container_size')
  raw=bytearray()
  for t in range(40):
   off=256+t*ts;h=data[off:off+256]
   if h[:12]!=b'Track-Info\r\n' or h[16]!=t or h[17]!=0 or h[20]!=2 or h[21]!=spt:raise Failure('track_header')
   for j in range(spt):
    if h[24+j*8:28+j*8]!=bytes([t,0,j+1,2]) or h[28+j*8:30+j*8]!=b'\0\0':raise Failure('sector_header')
   raw+=data[off+256:off+ts]
  raw=bytes(raw)
 else:raise Failure('unsupported_container')
 # Bounded layout checks, not a full filesystem checker or format detector.
 directory=bytearray();start=boot*spt*sec
 for n in range((maxdir*32+sec-1)//sec):
  t,s=divmod(n,spt);off=start+(t*spt+skew[s])*sec;directory+=raw[off:off+sec]
 blocks=(len(raw)-start)//block;reserved=(maxdir*32+block-1)//block;allocated={}
 for i in range(maxdir):
  e=directory[i*32:(i+1)*32]
  if e[0]==0xe5:continue
  if e[0]>15 or e[15]>128:raise Failure('directory_invalid')
  stem=bytes(x&127 for x in e[1:9]).decode('ascii',errors='replace').rstrip();ext=bytes(x&127 for x in e[9:12]).decode('ascii',errors='replace').rstrip()
  filename(f'{e[0]:02d}'+stem.lower()+('.'+ext.lower() if ext else ''),read_only)
  used=[int.from_bytes(e[j:j+(2 if blocks>256 else 1)],'little') for j in range(16,32,2 if blocks>256 else 1)]
  for b in used:
   if not b:continue
   if not reserved<=b<blocks or b in allocated:raise Failure('allocation_invalid')
   allocated[b]=i
 return {'raw':raw,'boot':raw[:start]}

def guard_rename_metadata(data,fmt,container):
 # The public experiment contract models only F1-F4/RO/SYS/ARC, not arbitrary bits.
 raw=validate(data,fmt,container)['raw']
 sec,tracks,spt,block,maxdir,boot,skew=FORMATS[fmt];directory=bytearray()
 for n in range((maxdir*32+sec-1)//sec):
  t,s=divmod(n,spt);off=(boot*spt+t*spt+skew[s])*sec;directory+=raw[off:off+sec]
 flags={}
 for i in range(maxdir):
  e=directory[i*32:(i+1)*32]
  if e[0]==0xe5:continue
  if any(x&128 for x in e[5:9]) or e[12]&0xe0 or e[14]&0xc0:raise Failure('unsupported_metadata','unmodelled directory bits')
  key=bytes([e[0]])+bytes(x&127 for x in e[1:12]);bits=bytes(x&128 for x in e[1:12])
  if key in flags and flags[key]!=bits:raise Failure('unsupported_metadata','attributes differ between extents')
  flags[key]=bits

class Adapter:
 def __init__(self,kind,timeout=5,cancel=None,fault=None,read_only=False):
  assert kind in ('helper','cli');self.read_only=read_only;self.kind=kind;self.timeout=timeout;self.cancel=cancel;self.fault=fault;self.trace=[]
 def child(self,args,cwd):
  # Fault hooks are test-only, never accepted from a request.
  if self.fault:args=self.fault(list(map(str,args)))
  args=list(map(str,args));env=dict(os.environ,LC_ALL='C',TZ='UTC');env.pop('CPMTOOLSFMT',None)
  if os.name=='nt':
   from windows_supervisor import supervise
   r=supervise(args,cwd,env,self.timeout,self.cancel)
   self.trace.append(dict(r,argv=args,stdout=r['stdout'].decode(errors='replace'),stderr=r['stderr'].decode(errors='replace')))
   if r['failure']:raise Failure(r['failure'])
   if r['code']<0 or r['code']>=0x80000000:raise Failure('backend_crash',str(r['code']))
   return r['code'],r['stdout'],r['stderr']
  p=subprocess.Popen(args,cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
  sel=selectors.DefaultSelector();out={'stdout':bytearray(),'stderr':bytearray()}
  for stream,name in [(p.stdout,'stdout'),(p.stderr,'stderr')]:os.set_blocking(stream.fileno(),False);sel.register(stream,selectors.EVENT_READ,name)
  began=time.monotonic();failure=None
  try:
   while sel.get_map() or p.poll() is None:
    if self.cancel is not None and self.cancel.is_set():failure='cancelled';break
    if time.monotonic()-began>self.timeout:failure='timeout';break
    for key,_ in sel.select(.01):
     b=os.read(key.fileobj.fileno(),65536)
     if not b:sel.unregister(key.fileobj);continue
     out[key.data]+=b
     if len(out[key.data])>262144:failure='output_limit';break
    if failure:break
  finally:
   if failure:
    try:os.killpg(p.pid,signal.SIGKILL)
    except ProcessLookupError:pass
   p.wait();sel.close();p.stdout.close();p.stderr.close()
   self.trace.append({'argv':args,'code':p.returncode,'failure':failure,'elapsed_ms':round((time.monotonic()-began)*1000,3),'stdout':out['stdout'][:262144].decode(errors='replace'),'stderr':out['stderr'][:262144].decode(errors='replace')})
  if failure:raise Failure(failure)
  if p.returncode<0:raise Failure('backend_crash',str(p.returncode))
  return p.returncode,bytes(out['stdout']),bytes(out['stderr'])
 def invoke(self,op,image,fmt,container,cwd,*args):
  if self.kind=='helper':
   rc,out,err=self.child([HELPER,op,image,fmt,container,*args],cwd)
   try:r=json.loads(out)
   except (ValueError,UnicodeError):raise Failure('backend_exit' if rc else 'protocol_invalid',err.decode(errors='replace'))
   if not isinstance(r,dict) or r.get('v')!=1 or type(r.get('ok'))!=bool:raise Failure('protocol_invalid')
   if not r['ok']:raise Failure(r.get('error','backend_error'),r.get('message',''))
   if rc:raise Failure('protocol_invalid','success with nonzero status')
   return r
  def cli(tool,*a):
   argv=[TOOLS/tool,'-f',fmt,'-T',container,*a,image] if tool=='cpmls' else [TOOLS/tool,'-f',fmt,'-T',container,image,*a]
   rc,out,err=self.child(argv,cwd)
   if rc:raise Failure('backend_error',err.decode(errors='replace'))
   return out.decode('ascii',errors='strict')
  if op=='list':
   entries={};user=None
   try:
    for line in cli('cpmls','-l').splitlines():
     if not line:continue
     if re.fullmatch(r'\d+:',line):user=int(line[:-1]);continue
     m=re.fullmatch(r'([-rwx]{10})\s+(\d+)\s+[A-Z][a-z]{2}\s+\d{2}\s+(?:\d{4}|\d{2}:\d{2})\s+(.+)',line)
     if not m or user is None:raise Failure('cli_parse')
     name=filename(f'{user:02d}'+m[3]);
     if name in entries:raise Failure('cli_parse','duplicate filename')
     entries[name]={'name':name,'size':int(m[2]),'attr':256 if 'w' not in m[1] else 0}
    user=None;seen=set()
    for line in cli('cpmls','-A').splitlines():
     if not line:continue
     if re.fullmatch(r'\d+:',line):user=int(line[:-1]);continue
     m=re.fullmatch(r'([1-][2-][3-][4-][s-][a-][r-][w-][e-]) (.+)',line)
     if not m or user is None:raise Failure('cli_parse')
     name=filename(f'{user:02d}'+m[2])
     if name not in entries or name in seen:raise Failure('cli_parse')
     seen.add(name);entries[name]['attr']+=sum(bit for bit,ch in zip([1,2,4,8,512,1024,8192,4096,2048],m[1]) if ch!='-')
    if seen!=set(entries):raise Failure('cli_parse','inconsistent listing modes')
   except UnicodeError:raise Failure('cli_parse','non-ASCII output')
   return {'entries':list(entries.values())}
  if op=='export':cli('cpmcp',str(int(args[0][:2]))+':'+args[0][2:],args[1]);return {}
  if op=='import':cli('cpmcp',args[0],str(int(args[1][:2]))+':'+args[1][2:]);return {}
  if op=='delete':cli('cpmrm',str(int(args[0][:2]))+':'+args[0][2:]);return {}
  if op=='attrs':cli('cpmchattr',args[1],str(int(args[0][:2]))+':'+args[0][2:]);return {}
  raise Failure('unsupported_operation')
 def listing(self,image,fmt,container,cwd):
  r=self.invoke('list',image,fmt,container,cwd);entries={}
  if not isinstance(r.get('entries'),list):raise Failure('protocol_invalid')
  for e in r['entries']:
   if not isinstance(e,dict) or not isinstance(e.get('name'),str):raise Failure('protocol_invalid')
   n=filename(e['name'],self.read_only)
   if n in entries or type(e.get('size'))!=int or not 0<=e['size']<=16777216 or type(e.get('attr'))!=int or not 0<=e['attr']<=16383:raise Failure('protocol_invalid')
   entries[n]={'name':n,'size':e['size'],'attr':e['attr']}
  return entries
 def export(self,image,fmt,container,cwd,name,path,entry):
  self.invoke('export',image,fmt,container,cwd,name,path)
  if not path.is_file() or path.stat().st_size!=entry['size']:raise Failure('export_unverified')
  return digest(path.read_bytes())
 def publish(self,staged,dest):
  # No replacement. A same-directory temporary and hard link publish without clobbering.
  fd,name=tempfile.mkstemp(prefix='.cpmimage-',dir=dest.parent);p=Path(name)
  try:
   with os.fdopen(fd,'wb') as out:out.write(staged.read_bytes());out.flush();os.fsync(out.fileno())
   try:os.link(p,dest)
   except FileExistsError:raise Failure('destination_exists')
  finally:p.unlink(missing_ok=True)
 def request(self,request,defs_override=None):
  begin=time.monotonic();rid=request.get('id') if isinstance(request,dict) else None;self.trace=[]
  try:
   if not isinstance(request,dict):raise Failure('invalid_request')
   if not isinstance(rid,str) or len(rid)>128:raise Failure('invalid_request')
   if request.get('v')!=1:raise Failure('request_version')
   op=request.get('op');fmt=request.get('format');container=request.get('container')
   if op not in ('list','export','rename'):raise Failure('unsupported_operation')
   if self.read_only and op=='rename':raise Failure('read_only')
   image=Path(request['image']).resolve()
   if not image.is_file() or image.stat().st_size>16777216:raise Failure('input_limit')
   original=image.read_bytes();original_hash=digest(original)
   geometry=validate(original,fmt,container,self.read_only)
   if op=='rename':guard_rename_metadata(original,fmt,container)
   dest=None
   if op in ('export','rename'):
    name=filename(request['name'],self.read_only);dest=Path(request['destination']).absolute()
    if dest.exists() or dest.is_symlink():raise Failure('destination_exists')
    if not dest.parent.is_dir():raise Failure('destination_parent')
   with tempfile.TemporaryDirectory(prefix='cpmimage-comparison-') as td:
    cwd=Path(td);(cwd/'diskdefs').write_text(DEFS if defs_override is None else defs_override)
    stage=cwd/('image.'+container);stage.write_bytes(original)
    entries=self.listing(stage,fmt,container,cwd)
    if op=='list':result={'entries':sorted(entries.values(),key=lambda e:e['name'])}
    else:
     if name not in entries:raise Failure('not_found')
     if op=='export':
      out=cwd/'export';h=self.export(stage,fmt,container,cwd,name,out,entries[name]);result={'sha256':h,'size':out.stat().st_size}
     else:
      target=filename(request['new_name'])
      if target in entries:raise Failure('name_collision')
      hashes={n:self.export(stage,fmt,container,cwd,n,cwd/('before-'+n),e) for n,e in entries.items()}
      if self.kind=='helper':self.invoke('rename',stage,fmt,container,cwd,name,target)
      else:
       # Copy/delete is confined to disposable snapshot; original is never incrementally edited.
       if entries[name]['attr']&~(1|2|4|8|256|512|1024):raise Failure('unsupported_attributes')
       # All bytes are already retained outside the image. Delete first on this disposable
       # snapshot, so full-disk/full-directory rename needs no extra image capacity.
       self.invoke('delete',stage,fmt,container,cwd,name)
       self.invoke('import',stage,fmt,container,cwd,cwd/('before-'+name),target)
       attrs='n'+''.join(ch for bit,ch in [(1,'1'),(2,'2'),(4,'3'),(8,'4'),(256,'r'),(512,'s'),(1024,'a')] if entries[name]['attr']&bit)
       self.invoke('attrs',stage,fmt,container,cwd,target,attrs)
      after=self.listing(stage,fmt,container,cwd);wanted=dict(entries);wanted[target]=dict(wanted.pop(name),name=target)
      if after!=wanted:raise Failure('rename_unverified','metadata or directory mismatch')
      for n,e in after.items():
       h=self.export(stage,fmt,container,cwd,n,cwd/('after-'+n),e)
       if h!=hashes[name if n==target else n]:raise Failure('rename_unverified','content mismatch')
      if validate(stage.read_bytes(),fmt,container)['boot']!=geometry['boot']:raise Failure('rename_unverified','reserved tracks changed')
      out=stage;result={'entries':sorted(after.values(),key=lambda e:e['name']),'sha256':digest(stage.read_bytes())}
     if digest(image.read_bytes())!=original_hash:raise Failure('source_changed')
     if self.cancel is not None and self.cancel.is_set():raise Failure('cancelled')
     self.publish(out,dest)
    if digest(image.read_bytes())!=original_hash:raise Failure('source_changed')
   return {'v':1,'id':rid,'ok':True,'result':result,'elapsed_ms':round((time.monotonic()-begin)*1000,3),'children':len(self.trace)}
  except Failure as e:return {'v':1,'id':rid,'ok':False,'error':e.code,'message':e.message,'elapsed_ms':round((time.monotonic()-begin)*1000,3),'children':len(self.trace)}
  except (OSError,KeyError,TypeError,ValueError) as e:return {'v':1,'id':rid,'ok':False,'error':'request_or_io','message':str(e),'children':len(self.trace)}

if __name__=='__main__':
 import sys
 print(json.dumps(Adapter(sys.argv[1]).request(json.load(sys.stdin))))
