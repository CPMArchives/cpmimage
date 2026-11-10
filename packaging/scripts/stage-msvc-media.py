"""Prepare an offline, account-free native ARM64 development kit from verified payloads."""
from pathlib import Path
import shutil,zipfile
base=Path(__file__).resolve().parents[1];media=base/'build-media';media.mkdir(exist_ok=True)
def copy(source,dest):
 if source.is_dir():shutil.copytree(source,dest,dirs_exist_ok=True)
 else:dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
copy(base/'msvc-extracted/VC',media/'MSVC/VC')
sdk=base/'sdk-extracted/Program Files/Windows Kits/10'; dest=media/'SDK'
for sub in ['Include/10.0.26100.0','Licenses/10.0.26100.0','Lib/10.0.26100.0/ucrt/arm64','Lib/10.0.26100.0/um/arm64','bin/10.0.26100.0/arm64']:
 if (sdk/sub).exists():copy(sdk/sub,dest/sub)
copy(base/'qt-msvc',media/'Qt')
if not (media/'CMake/bin/cmake.exe').exists():
 with zipfile.ZipFile(base/'downloads/cmake-4.4.5-windows-arm64.zip') as z:
  for name in z.namelist():
   parts=Path(name).parts
   if len(parts)>1 and not name.endswith('/'):
    out=media/'CMake'/Path(*parts[1:]);out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(z.read(name))
copy(base/'licenses',media/'license-texts')
# Each developer must review/accept applicable terms; this script does not do so.
