# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows-only experimental pipe/process-tree supervisor; validate in the test VM."""
import ctypes,queue,subprocess,threading,time
from ctypes import wintypes as W

def supervise(args,cwd,env,timeout,cancel):
 k=ctypes.WinDLL('kernel32',use_last_error=True)
 class Basic(ctypes.Structure):
  _fields_=[('process_time',ctypes.c_int64),('job_time',ctypes.c_int64),('flags',W.DWORD),('min_ws',ctypes.c_size_t),('max_ws',ctypes.c_size_t),('active',W.DWORD),('affinity',ctypes.c_size_t),('priority',W.DWORD),('scheduling',W.DWORD)]
 class IO(ctypes.Structure):_fields_=[(n,ctypes.c_uint64) for n in ('readops','writeops','otherops','readbytes','writebytes','otherbytes')]
 class Extended(ctypes.Structure):_fields_=[('basic',Basic),('io',IO),('process_memory',ctypes.c_size_t),('job_memory',ctypes.c_size_t),('peak_process',ctypes.c_size_t),('peak_job',ctypes.c_size_t)]
 k.CreateJobObjectW.argtypes=[ctypes.c_void_p,W.LPCWSTR];k.CreateJobObjectW.restype=W.HANDLE
 k.SetInformationJobObject.argtypes=[W.HANDLE,ctypes.c_int,ctypes.c_void_p,W.DWORD];k.SetInformationJobObject.restype=W.BOOL
 k.AssignProcessToJobObject.argtypes=[W.HANDLE,W.HANDLE];k.AssignProcessToJobObject.restype=W.BOOL
 k.TerminateJobObject.argtypes=[W.HANDLE,W.UINT];k.TerminateJobObject.restype=W.BOOL
 k.CloseHandle.argtypes=[W.HANDLE];k.CloseHandle.restype=W.BOOL
 job=k.CreateJobObjectW(None,None)
 if not job:raise ctypes.WinError(ctypes.get_last_error())
 p=None;began=time.monotonic();out={'stdout':bytearray(),'stderr':bytearray()};failure=None
 try:
  limits=Extended();limits.basic.flags=0x2000 # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
  if not k.SetInformationJobObject(job,9,ctypes.byref(limits),ctypes.sizeof(limits)):raise ctypes.WinError(ctypes.get_last_error())
  p=subprocess.Popen(args,cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NEW_PROCESS_GROUP|subprocess.CREATE_NO_WINDOW)
  if not k.AssignProcessToJobObject(job,W.HANDLE(int(p._handle))):
   error=ctypes.get_last_error();p.kill();p.wait();raise ctypes.WinError(error)
  q=queue.Queue(maxsize=16);stop=threading.Event()
  def send(item):
   while not stop.is_set():
    try:q.put(item,timeout=.05);return
    except queue.Full:pass
  def reader(stream,name):
   try:
    while True:
     b=stream.read(65536)
     if not b:break
     send((name,b))
   except OSError:pass
   finally:send((name,None))
  threads=[threading.Thread(target=reader,args=(s,n),daemon=True) for s,n in ((p.stdout,'stdout'),(p.stderr,'stderr'))]
  for t in threads:t.start()
  done=set()
  while len(done)<2 or p.poll() is None:
   if cancel is not None and cancel.is_set():failure='cancelled';break
   if time.monotonic()-began>timeout:failure='timeout';break
   try:name,b=q.get(timeout=.01)
   except queue.Empty:continue
   if b is None:done.add(name);continue
   room=262145-len(out[name]);out[name]+=b[:max(0,room)]
   if len(out[name])>262144:failure='output_limit';break
  if failure:k.TerminateJobObject(job,0xC000013A)
  p.wait();stop.set()
  # Closing the job also removes lingering descendants before readers are joined.
  k.CloseHandle(job);job=None
  for t in threads:t.join(timeout=2)
  p.stdout.close();p.stderr.close()
  return {'code':p.returncode,'failure':failure,'elapsed_ms':round((time.monotonic()-began)*1000,3),'stdout':bytes(out['stdout'][:262144]),'stderr':bytes(out['stderr'][:262144])}
 finally:
  if job:k.CloseHandle(job)
  if p is not None and p.poll() is None:p.kill();p.wait()
