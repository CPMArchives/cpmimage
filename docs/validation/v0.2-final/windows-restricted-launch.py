# SPDX-License-Identifier: GPL-3.0-or-later
# Run-specific native qualification harness; paths identify disposable VM test data.
import ctypes,json,traceback
from ctypes import wintypes as w
from pathlib import Path
k=ctypes.WinDLL('kernel32',use_last_error=True);a=ctypes.WinDLL('advapi32',use_last_error=True)
class SA(ctypes.Structure):_fields_=[('Sid',w.LPVOID),('Attributes',w.DWORD)]
class SI(ctypes.Structure):_fields_=[('cb',w.DWORD),('lpReserved',w.LPWSTR),('lpDesktop',w.LPWSTR),('lpTitle',w.LPWSTR),('dwX',w.DWORD),('dwY',w.DWORD),('dwXSize',w.DWORD),('dwYSize',w.DWORD),('dwXCountChars',w.DWORD),('dwYCountChars',w.DWORD),('dwFillAttribute',w.DWORD),('dwFlags',w.DWORD),('wShowWindow',w.WORD),('cbReserved2',w.WORD),('lpReserved2',ctypes.POINTER(ctypes.c_ubyte)),('hStdInput',w.HANDLE),('hStdOutput',w.HANDLE),('hStdError',w.HANDLE)]
class PI(ctypes.Structure):_fields_=[('hProcess',w.HANDLE),('hThread',w.HANDLE),('dwProcessId',w.DWORD),('dwThreadId',w.DWORD)]
k.GetCurrentProcess.restype=w.HANDLE;k.CloseHandle.argtypes=[w.HANDLE]
a.OpenProcessToken.argtypes=[w.HANDLE,w.DWORD,ctypes.POINTER(w.HANDLE)]
a.ConvertStringSidToSidW.argtypes=[w.LPCWSTR,ctypes.POINTER(w.LPVOID)]
a.CreateRestrictedToken.argtypes=[w.HANDLE,w.DWORD,w.DWORD,ctypes.POINTER(SA),w.DWORD,w.LPVOID,w.DWORD,w.LPVOID,ctypes.POINTER(w.HANDLE)]
a.GetTokenInformation.argtypes=[w.HANDLE,ctypes.c_int,w.LPVOID,w.DWORD,ctypes.POINTER(w.DWORD)]
a.ConvertSidToStringSidW.argtypes=[w.LPVOID,ctypes.POINTER(w.LPWSTR)]
a.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes=[w.LPCWSTR,w.DWORD,ctypes.POINTER(w.LPVOID),ctypes.POINTER(w.DWORD)]
a.GetSecurityDescriptorDacl.argtypes=[w.LPVOID,ctypes.POINTER(w.BOOL),ctypes.POINTER(w.LPVOID),ctypes.POINTER(w.BOOL)]
a.SetTokenInformation.argtypes=[w.HANDLE,ctypes.c_int,w.LPVOID,w.DWORD]
a.GetLengthSid.argtypes=[w.LPVOID];a.GetLengthSid.restype=w.DWORD
k.LocalFree.argtypes=[w.LPVOID]
a.CreateProcessAsUserW.argtypes=[w.HANDLE,w.LPCWSTR,w.LPWSTR,w.LPVOID,w.LPVOID,w.BOOL,w.DWORD,w.LPVOID,w.LPCWSTR,ctypes.POINTER(SI),ctypes.POINTER(PI)]
def check(ok):
 if not ok:raise ctypes.WinError(ctypes.get_last_error())
result={}
try:
 token=w.HANDLE();check(a.OpenProcessToken(k.GetCurrentProcess(),0x02000000,ctypes.byref(token)))
 admin=w.LPVOID();medium=w.LPVOID();check(a.ConvertStringSidToSidW('S-1-5-32-544',ctypes.byref(admin)));check(a.ConvertStringSidToSidW('S-1-16-8192',ctypes.byref(medium)))
 deny=SA(admin,0);limited=w.HANDLE();check(a.CreateRestrictedToken(token,2,1,ctypes.byref(deny),0,None,0,None,ctypes.byref(limited)))
 # A normal user token grants its user SID control over new child objects.
 needed=w.DWORD();a.GetTokenInformation(token,1,None,0,ctypes.byref(needed));userbuf=ctypes.create_string_buffer(needed.value);check(a.GetTokenInformation(token,1,userbuf,needed.value,ctypes.byref(needed)))
 usersid=ctypes.cast(userbuf,ctypes.POINTER(w.LPVOID)).contents;usertext=w.LPWSTR();check(a.ConvertSidToStringSidW(usersid,ctypes.byref(usertext)))
 sd=w.LPVOID();check(a.ConvertStringSecurityDescriptorToSecurityDescriptorW('D:(A;;GA;;;SY)(A;;GA;;;'+usertext.value+')',1,ctypes.byref(sd),None))
 present=w.BOOL();defaulted=w.BOOL();dacl=w.LPVOID();check(a.GetSecurityDescriptorDacl(sd,ctypes.byref(present),ctypes.byref(dacl),ctypes.byref(defaulted)))
 check(a.SetTokenInformation(limited,6,ctypes.byref(dacl),ctypes.sizeof(dacl)));check(a.SetTokenInformation(limited,4,ctypes.byref(usersid),ctypes.sizeof(usersid)))
 label=SA(medium,0x20);check(a.SetTokenInformation(limited,25,ctypes.byref(label),ctypes.sizeof(label)+a.GetLengthSid(medium)))
 si=SI();si.cb=ctypes.sizeof(si);pi=PI();command=ctypes.create_unicode_buffer('C:\\V02\\cpmimage.exe')
 check(a.CreateProcessAsUserW(limited,None,command,None,None,False,0,None,'C:\\Users\\cpmarchive',ctypes.byref(si),ctypes.byref(pi)))
 result={'pid':pi.dwProcessId,'administrator_sid':'deny-only','maximum_privileges':'disabled','integrity':'medium','default_dacl':'user and SYSTEM full access to newly created objects','token_owner':'user SID','system_security_settings_changed':False}
 for h in [pi.hThread,pi.hProcess,limited,token]:k.CloseHandle(h)
 k.LocalFree(admin);k.LocalFree(medium)
except Exception:result={'error':traceback.format_exc()}
Path('C:/CPMImageFinal/restricted-launch.json').write_text(json.dumps(result,indent=2)+'\n')
