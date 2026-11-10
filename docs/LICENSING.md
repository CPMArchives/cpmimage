# Licensing and distribution materials

New cpmimage application, helper integration, broker, tests and packaging code is **GPL-3.0-or-later**, approved by the owner on 2026-11-10. The proposed source commit applies that license through root `LICENSE` and SPDX notices. Third-party code is not relicensed by this choice.

## Components

| Component | Version | Treatment |
|---|---|---|
| cpmtools | 2.23 | GPL-3.0-or-later; helper links internal filesystem/device objects |
| libdsk | 1.4.2 | Library-GPL-2.0-or-later and other retained source notices; combined helper is GPL3+ |
| Qt Core/Gui/Widgets | 6.12.0 qualification builds | Shared LGPL3 libraries with upstream third-party terms; Qt Test is development-only |
| CPython | 3.13.17 packages | PSF/historical and embedded-component terms, including HACL/mimalloc |
| LLVM-MinGW/MinGW/libffi | Windows C helper/runtime inputs | Original notices retained; no LLVM C++ runtime DLL is required by the helper |
| Microsoft CRT | release ARM64 VC143 runtime | Unmodified distributable runtime; retains Microsoft licensing |

The cpmtools/libdsk bridge patch has a dated modification notice and is included in `packaging/source`. libdsk itself is unchanged. The process boundary does not remove copyleft obligations from the linked GPL helper.

`licenses/third-party` preserves the assembled upstream texts, copyright notices, Qt attribution records, cpmtools local-change notice, CPython notices, MinGW notices, and Microsoft terms. This is deliberately a conservative superset of upstream attributions. `packaging/scripts/notices.py` regenerates product notices from the exact pinned source trees. Platform binaries, toolchains and downloaded dependency archives are not committed.

## Binary distribution requirements

The packaging workflow supplies a separate corresponding-source archive containing application/helper source, the exact cpmtools/libdsk/Qt/Python source archives, the patch and build scripts, and current configuration/SBOM material where available. Do not distribute a binary bundle without its matching notices and source companion. A web link to an upstream project alone is not the source plan.

Shared Qt components can be replaced/rebuilt; the application contains no DRM or signature-enforcement restriction on that right. A modified local Mac bundle can be ad-hoc re-signed with `codesign --force --deep --sign - /path/to/CPMImage.app`. No Apple developer identity is required for that local step. This is not notarization or permission to distribute a public release.

MSVC/Windows SDK development tools are not part of the product or GPL source companion. Each developer must obtain and accept the applicable vendor terms independently. Download/staging scripts do not establish another person's license or record automatic acceptance. The retained terms are provenance and notice material, not a transferable record of the original local VM user's acceptance. See [Microsoft's redistributable list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) before any public Windows binary release.

The source commit is an engineering milestone, not a legal opinion or a completed public-release compliance signoff.
