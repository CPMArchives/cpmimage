from pathlib import Path
import zipfile
b=Path(__file__).resolve().parents[1];media=b/'build-media'
um_libs=set('kernel32 user32 gdi32 winspool shell32 ole32 oleaut32 uuid comdlg32 advapi32 ntdll bcrypt crypt32 secur32 userenv version imm32 winmm ws2_32 rpcrt4 setupapi dbghelp d3d11 d3d12 dxgi dwrite uiautomationcore runtimeobject propsys powrprof uxtheme shlwapi pathcch shcore mpr netapi32 winhttp wininet synchronization windowsapp cfgmgr32 iphlpapi psapi opengl32 glu32 comctl32 dxguid d2d1 dwmapi wtsapi32'.split())
vc_libs=set('msvcrt msvcprt vcruntime oldnames vcstartup msvcmrt msvcmrt_netcore diaguids pgort'.split())
with zipfile.ZipFile(b/'downloads/msvc-offline-kit-compact.zip','w',zipfile.ZIP_LZMA) as z:
 for top in ['MSVC','SDK','Qt','CMake','license-texts']:
  for f in (media/top).rglob('*'):
   if not f.is_file():continue
   rel=f.relative_to(media);s=str(rel).lower();name=f.name.lower()
   if f.suffix.lower() in ('.pdb','.prl') or '/debug_nonredist/' in s or '/enclave/' in s:continue
   if top=='MSVC' and '/lib/arm64/' in s and f.suffix.lower()=='.lib' and f.stem.lower() not in vc_libs:continue
   if top=='SDK':
    if '/um/arm64/' in s and f.suffix.lower()=='.lib' and f.stem.lower() not in um_libs:continue
    if '/ucrt/arm64/' in s and name!='ucrt.lib':continue
    if '/cppwinrt/' in s or '/winrt/' in s or name.endswith(('.idl','.winmd','.tlb')):continue
    if name.startswith(('mshtml','mshtmhst')):continue
   if top=='Qt':
    if name.endswith('-debug.cmake'):continue
    if name.endswith(('d.dll','d.lib')) and f.with_name(f.stem[:-1]+f.suffix).exists():continue
    if any(x in s for x in ['/sbom/','/doc/','/share/','/modules/','/metatypes/']):continue
   if top=='CMake':
    if '/bin/' in s and name not in ['cmake.exe','ctest.exe']:continue
    if not ('/bin/' in s or '/modules/' in s or '/templates/' in s or 'copyright' in s):continue
   z.write(f,rel)
print((b/'downloads/msvc-offline-kit-compact.zip').stat().st_size,flush=True)
