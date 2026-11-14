# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble source-backed license notices without assuming all bundled code is GPL."""
import hashlib,json,re,shutil,tarfile
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]; ROOT=BASE.parent
QT=ROOT/'prototype/qt-cross/qtbase-everywhere-src-6.12.0'
PYTHON=BASE/'build-python-macos/Python-3.13.17'
if not QT.exists(): QT=BASE/'qt-source/qtbase-everywhere-src-6.12.0'
if not PYTHON.exists(): PYTHON=BASE/'build-python-linux/Python-3.13.17'

def copy(source,dest):
 dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)

def create(destination,platform):
 destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
 copy(ROOT/'prototype/LICENSE',destination/'GPL-3.0.txt')
 copy(ROOT/'prototype/licenses/libdsk-Library-GPL-2.0.txt',destination/'libdsk-Library-GPL-2.0.txt')
 with tarfile.open(ROOT/'vendor/cpmtools-2.23.tar.gz') as cpm:
  (destination/'cpmtools-README.txt').write_bytes(cpm.extractfile('cpmtools-2.23/README').read())
 (destination/'cpmtools-local-modifications.txt').write_text('CPMImage local modification,2026-11-10: cpmtools2.23 device_libdsk.c geometry fallback preserves odd logical track counts as single-sided rather than dropping the last track. Explicit geometry and successful probes retain priority. See corresponding-source packaging/source/cpmtools-2.23-odd-tracks-libdsk.patch and build recipes. libdsk1.4.2 itself is unchanged.\nCPMImage read-only-name modification, 2026-11-10: a dedicated cpmfs-readonly.o accepts colon as opaque directory-name data under CPMIMAGE_READ_ONLY_NAMES. Ordinary CLI objects retain their original validation. See packaging/source/cpmtools-2.23-readonly-names.patch.\nCPMImage diskdefs modification, 2026-11-14: validate parsed definitions and read-only directories and add a byte-offset RAW device. See packaging/source/cpmtools-2.23-diskdefs.patch.\nCPMImage formatter modifications: correct reserved-sector assertion and apply cpmtools block addressing to directory initialization. See cpmtools-2.23-bootsec-assert.patch and cpmtools-2.23-mkfs-addressing.patch.\n')
 if platform == 'macos':
  (destination/'Qt-local-modifications.txt').write_text('CPMImage macOS Qt modification, 2026-11-10: backport upstream commit b74d6cb3c5e8418602494ae549b60839d88ef858 (QTBUG-149612) to Qt 6.12.0. Native accessibility element teardown no longer deletes interfaces owned by the item view/accessibility cache. Upstream author and review metadata are retained in corresponding-source packaging/source/qtbase-6.12.0-cocoa-accessibility.patch. Windows/Linux builds do not apply this Cocoa-only patch.\n')
 copy(PYTHON/'LICENSE',destination/'CPython-LICENSE.txt')
 copy(PYTHON/'Doc/license.rst',destination/'CPython-full-third-party-notices.txt')
 # Full LGPL/GPL exceptions and all upstream Qt attribution records retained.
 for f in (QT/'LICENSES').glob('*'):
  if f.is_file():copy(f,destination/'Qt-LICENSES'/f.name)
 records=[]
 for f in sorted(QT.rglob('qt_attribution.json'))+sorted(QT.rglob('qt_attributions.json')):
  copy(f,destination/'Qt-upstream-notices'/f.relative_to(QT))
  data=json.loads(f.read_text(),strict=False);items=data if isinstance(data,list) else [data]
  for item in items:
   record=dict(item,SourceAttribution=str(f.relative_to(QT)));records.append(record)
   names=item.get('LicenseFile',[]) or []
   if isinstance(names,str):names=[names]
   for name in names:
    src=(f.parent/name).resolve()
    if src.is_file() and QT.resolve() in src.parents:
     copy(src,destination/'Qt-upstream-notices'/src.relative_to(QT.resolve()))
 (destination/'Qt-upstream-attributions.json').write_text(json.dumps(records,indent=2,ensure_ascii=False)+'\n')
 text=[]
 for item in records:
  copyright=item.get('Copyright','')
  if isinstance(copyright,list):copyright='\n'.join(copyright)
  text.append(str(item.get('Name',item.get('Id','Qt component')))+'\n'+str(copyright)+'\nLicense: '+str(item.get('License',''))+'\nSource notice: '+item['SourceAttribution'])
 (destination/'Qt-third-party-copyright-notices.txt').write_text('\n\n'.join(text)+'\n')
 # HACL license is embedded in generated CPython source headers, rather than a LICENSE file.
 comments=set()
 for f in (PYTHON/'Modules/_hacl').rglob('*'):
  if f.suffix not in ('.h','.c'):continue
  text=f.read_text(errors='replace');match=re.search(r'/\*.*?\*/',text,re.S)
  if match and 'Copyright' in match[0] and 'Permission' in match[0]:comments.add(match[0])
 (destination/'CPython-HACL-notices.txt').write_text('\n\n'.join(sorted(comments))+'\n')
 with tarfile.open(ROOT/'vendor/libdsk-1.4.2.tar.gz') as archive:
  records=[]
  for member in archive.getmembers():
   if member.isfile() and member.name.endswith(('.h','.c')):
    text=archive.extractfile(member).read().decode(errors='replace')
    for comment in re.findall(r'/\*.*?\*/',text,re.S):
     if any(x in comment.lower() for x in ['copyright','license','permission']):records.append(member.name+'\n'+comment)
  (destination/'libdsk-source-header-notices.txt').write_text('\n\n'.join(records)+'\n')
 if platform=='windows':
  tc=ROOT/'windows-runtime/llvm-mingw-20261104-ucrt-macos-universal'
  copy(tc/'LICENSE.TXT',destination/'LLVM-MinGW-LICENSE.txt')
  for f in (tc/'aarch64-w64-mingw32/share/mingw32').glob('COPYING*'):
   copy(f,destination/'MinGW-notices'/f.name)
  copy(ROOT/'windows-runtime/offline-media/CPMImageTests/python/LICENSE.txt',destination/'CPython-Windows-distribution-LICENSE.txt')
  copy(BASE/'licenses/Visual-Studio-Community-license.docx',destination/'Microsoft-Community-terms.docx')
  copy(BASE/'licenses/Visual-Studio-Community-license.txt',destination/'Microsoft-Community-terms.txt')
  copy(BASE/'licenses/Windows-SDK-license.txt',destination/'Microsoft-Windows-SDK-terms.txt')
  copy(BASE/'licenses/MSVC-BuildTools-license.txt',destination/'Microsoft-BuildTools-terms.txt')
 summary='''CPMImage read-only review package — THIRD-PARTY NOTICES

Application and helper integration code: GPL-3.0-or-later, as approved by the owner.
You may copy, modify and redistribute it under that license. There is no warranty.
The GPL does not replace the licenses of the separate third-party components below.

cpmtools 2.23 — GPL-3.0-or-later. Copyright Michael Haardt and contributors.
The helper links cpmtools internal filesystem/device code.
Local read-only-name modification (2026-11-10): the dedicated helper accepts
colon as an opaque directory-name character; CLI tools retain their original
validation. The diskdefs patch validates definitions and read-only directories
and adds a byte-offset RAW device. See packaging/source/cpmtools-2.23-*.patch, GPL-3.0.txt,
cpmtools-README.txt, and the complete source/build material beside this package.

libdsk 1.4.2 — GNU Library GPL v2-or-later in most files; GPL v2-or-later and
other retained notices in included components. The combined GPL helper is
provided under GPL-3.0-or-later. Retained texts/header notices are included.

Qt 6.12.0 Core, Gui, Widgets and platform plugins — LGPL-3.0, with Qt's
third-party components retaining their own notices. Copyright The Qt Company
Ltd. and contributors. Complete license texts and upstream attribution records
are included in Qt-LICENSES/ and Qt-upstream-notices/. This archive intentionally
retains notices for upstream components beyond those enabled in this package.
The build inventory/configuration records identify the actual shipped libraries.
JPEG support is based in part on the work of the Independent JPEG Group.
Portions of this software are copyright the FreeType Project (freetype.org).
All rights reserved. Detailed years and authors are retained in the FreeType
notice and Qt-third-party-copyright-notices.txt.

You may replace or rebuild the shared Qt libraries, modify this application, and
reverse engineer it to debug your Qt modifications as permitted by the LGPL.
Do not interpret the package signature as a restriction on those rights. For the
local Mac package, re-sign modified binaries/app with an ad-hoc identity as
explained in the package README. There is no DRM or signature-check enforcement
in the application. Corresponding Qt/application sources and build recipes are
provided beside the binary package, not solely through an upstream web link.

CPython 3.13.17 — Python Software Foundation License v2 and historical notices.
Copyright Python Software Foundation and contributors. See CPython-LICENSE.txt
and CPython-HACL-notices.txt, plus CPython-full-third-party-notices.txt
(including mimalloc and other embedded-component licenses). The private interpreter is bundled; installed
system Python is not needed. Windows also retains the upstream embedded
Python distribution's full notice document, including unused component notices.

Windows packages additionally use the Microsoft Visual C++ runtime and libffi
through the CPython ctypes module. They retain their Microsoft and MIT terms;
see the Microsoft terms and CPython Windows license document. The compiler/SDK
and CMake development tools are not shipped as part of the application package.
The isolated C helper is built using LLVM-MinGW; its relevant upstream notices
are retained with the source materials. No LLVM C++ runtime DLL is needed by it.

Operating-system components (Apple frameworks, Windows system DLLs, or the
Linux glibc/graphics stack) are prerequisites supplied by the operating system,
not relicensed or copied into the app by this notice. See README for the tested
platform baseline. This review package is not a signed/notarized public release.
'''
 (destination/'THIRD-PARTY-NOTICES.txt').write_text(summary)
 files={str(f.relative_to(destination)):hashlib.sha256(f.read_bytes()).hexdigest() for f in destination.rglob('*') if f.is_file() and f.name!='notice-sha256.json'}
 (destination/'notice-sha256.json').write_text(json.dumps(files,indent=2)+'\n')
