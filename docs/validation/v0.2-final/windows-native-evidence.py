# SPDX-License-Identifier: GPL-3.0-or-later
# Run-specific native qualification harness; paths identify disposable VM test data.
from pathlib import Path
import subprocess,csv,ctypes,json,hashlib
from ctypes import wintypes
class SA(ctypes.Structure):_fields_=[('Sid',wintypes.LPVOID),('Attributes',wintypes.DWORD)]
class TG(ctypes.Structure):_fields_=[('GroupCount',wintypes.DWORD),('Groups',SA*1)]
k=ctypes.WinDLL('kernel32',use_last_error=True);a=ctypes.WinDLL('advapi32',use_last_error=True)
k.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];k.OpenProcess.restype=wintypes.HANDLE
k.QueryFullProcessImageNameW.argtypes=[wintypes.HANDLE,wintypes.DWORD,wintypes.LPWSTR,ctypes.POINTER(wintypes.DWORD)]
a.OpenProcessToken.argtypes=[wintypes.HANDLE,wintypes.DWORD,ctypes.POINTER(wintypes.HANDLE)]
a.ConvertSidToStringSidW.argtypes=[wintypes.LPVOID,ctypes.POINTER(wintypes.LPWSTR)]
k.LocalFree.argtypes=[wintypes.LPVOID]
a.IsTokenRestricted.argtypes=[wintypes.HANDLE];a.IsTokenRestricted.restype=wintypes.BOOL
a.GetSidSubAuthorityCount.argtypes=[wintypes.LPVOID];a.GetSidSubAuthorityCount.restype=ctypes.POINTER(ctypes.c_ubyte)
a.GetSidSubAuthority.argtypes=[wintypes.LPVOID,wintypes.DWORD];a.GetSidSubAuthority.restype=ctypes.POINTER(wintypes.DWORD)
a.GetTokenInformation.argtypes=[wintypes.HANDLE,ctypes.c_int,wintypes.LPVOID,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD)]
k.CloseHandle.argtypes=[wintypes.HANDLE]
processes=[]
for row in csv.reader(subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True).splitlines()):
 if row and row[0].lower()=='cpmimage.exe':
  pid=int(row[1]);h=k.OpenProcess(0x1000,False,pid);t=wintypes.HANDLE();record={'pid':pid}
  if not h:record['error']='OpenProcess failed'
  else:
   name=ctypes.create_unicode_buffer(32768);size=wintypes.DWORD(len(name))
   if k.QueryFullProcessImageNameW(h,0,name,ctypes.byref(size)):record['executable']=name.value
   if a.OpenProcessToken(h,0x0008,ctypes.byref(t)):
    record['restricted']=bool(a.IsTokenRestricted(t))
    groupbytes=wintypes.DWORD();a.GetTokenInformation(t,2,None,0,ctypes.byref(groupbytes));groups=ctypes.create_string_buffer(groupbytes.value)
    if a.GetTokenInformation(t,2,groups,groupbytes.value,ctypes.byref(groupbytes)):
     count=ctypes.cast(groups,ctypes.POINTER(wintypes.DWORD)).contents.value
     entries=ctypes.cast(ctypes.addressof(groups)+TG.Groups.offset,ctypes.POINTER(SA))
     for index in range(count):
      sidtext=wintypes.LPWSTR()
      if a.ConvertSidToStringSidW(entries[index].Sid,ctypes.byref(sidtext)):
       if sidtext.value=='S-1-5-32-544':record['administrators_sid_attributes']=entries[index].Attributes
       k.LocalFree(sidtext)
    needed=wintypes.DWORD();a.GetTokenInformation(t,25,None,0,ctypes.byref(needed))
    buf=ctypes.create_string_buffer(needed.value)
    if a.GetTokenInformation(t,25,buf,needed.value,ctypes.byref(needed)):
     sid=ctypes.cast(buf,ctypes.POINTER(ctypes.c_void_p)).contents.value
     count=a.GetSidSubAuthorityCount(sid).contents.value
     record['integrity_rid']=a.GetSidSubAuthority(sid,count-1).contents.value
    for label,info in [('elevated',20),('elevation_type',18)]:
     value=wintypes.DWORD();needed=wintypes.DWORD()
     if a.GetTokenInformation(t,info,ctypes.byref(value),ctypes.sizeof(value),ctypes.byref(needed)):record[label]=value.value
    k.CloseHandle(t)
   k.CloseHandle(h)
  processes.append(record)
root=Path('C:/CPMImageFinal/native-output')
hashes={str(p.relative_to(root)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in root.rglob('*') if p.is_file()}
source=Path('C:/CPMImageFinal/native.raw')
Path('C:/CPMImageFinal/native-followup.json').write_text(json.dumps({'process_tokens':processes,'exports':hashes,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'directories':[str(p.relative_to(root)) for p in root.rglob('*') if p.is_dir()],'basic_user_exports':{str(p.relative_to(Path('C:/Users/cpmarchive/AppData/Local/Temp/CPMImageBasicTest'))):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in Path('C:/Users/cpmarchive/AppData/Local/Temp/CPMImageBasicTest').rglob('*') if p.is_file()}},indent=2)+'\n')
