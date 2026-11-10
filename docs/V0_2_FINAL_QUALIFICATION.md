# v0.2 final qualification review — 2026-11-11

Application source: merged PR #30, `c467c34`. Branch: `work/v0.2-final-qualification`.
No application behavior changes in this proposal. No push, release, or GitHub changes.

## Result

Automated regression, relocated ARM64 package checks, and the remaining native desktop checks are complete on macOS, Windows, and Linux. **Ready for review and v0.2 sign-off within the scope below.** Windows/Linux manual checks completed by the user are explicitly attributed; independent filesystem/hash verification accompanies them. No new application fix was required in this follow-up. Nothing has been pushed or released.

| Check | macOS ARM64 | Windows 11 ARM64 | Debian 13 ARM64 |
|---|---:|---:|---:|
| Qt application regression, offscreen | 26 pass | 24 pass, 2 skip | 26 pass |
| Broker regression | 11 pass | 9 pass, 2 skip | 11 pass |
| Final-track fixtures | 6 pass | 6 pass | 6 pass |
| Relocated packaged application diagnostics and broker | pass | pass | pass |
| Exact names and three independent export hashes | pass | pass | pass |
| No-overwrite refusal and 20 sequential exports | pass | pass | pass |
| Six final-track exports from package | pass | pass | pass |
| Source and package unchanged during package checks | pass | pass | pass |
| Native desktop launch/open/list | pass | pass | pass |
| Native multi-file export output hashes | pass; report and responsiveness verified in follow-up | pass, visible report and hashes | pass, visible report and hashes |

The Windows skips require POSIX fault-injection helper scripts: two Qt in-flight cancellation/close cases and two broker process/fault cases. They are skips, not passes. Other normal cancellation coverage remains in the Qt suite.

## Changes proposed

* Discover the private macOS Python executable's actual absolute libpython dependency and rewrite it to the package-relative location. Reusing a runtime built in another checkout exposed the previous hard-coded prefix assumption.
* Update package verification to require the uppercase CP/M listing identities and verify the exact set, rather than just its count. Three checksum oracles and the existing no-overwrite/final-track checks remain independent of the listing.
* Record qualification evidence and its limits. The application itself remains at the merged PR #30 implementation.

## Relocatable packages

Packages were built with Qt 6.12.0 and the approved read-only cpmtools/libdsk helper. Windows GUI was built natively with MSVC 19.44; Debian with GCC 14.2; macOS with Apple Clang 21.0. Native runtime libraries, private Python, helper, disk definitions, and license notices are included.

All three packages were moved outside build directories into paths containing spaces and `ü`. Package verification clears development loader/plugin paths, poisons PYTHONHOME/PYTHONPATH, and restricts PATH. macOS and Linux runtime/dependency prefixes were temporarily hidden and restored. Windows' old Qt and previous package directories were hidden and restored; hiding the entire MSVC development directory was blocked by an open handle, so the narrower isolation was used. The successful run records no failures to hide those two narrower prefixes.

Binary audits inspected 61 Mach-O files, 28 ARM64 PE files, and 64 ELF files. macOS dependencies and rpaths are package-relative or OS supplied, and ad-hoc signatures verify. Windows imports are bundled or OS supplied. Linux retains its documented system glibc/X11 requirements. The relocated macOS package contains the previously validated Qt Cocoa patch.

Local archives, corresponding source, and checksums are generated under `packaging/dist/`; generated binaries and upstream archives are excluded from the proposed source commit. The companion includes pinned upstream sources and available build configuration/SBOM records. This is a local review bundle, not a published release.

## Completed native desktop checks

* macOS: reconnected to the same relocated Cocoa package after the earlier automation timeout. Observed the two-file export report, including `SIG/M.LIB → SIG_M.LIB`, closed it, opened Image Information, verified the displayed export hash, and closed it. Continued responsiveness is verified; the earlier timeout was not reproduced.
* Windows: agent observed native multi-file export, then repeated it, observed two destination conflicts and chose Skip existing. The app reported two exports on the first run and two skips on the second. Both output hashes match the independent oracles and the source is unchanged. The user completed populated/empty whole-area exports and confirmed the checks pass. Filesystem verification found `0` containing both correct files and an empty `1` folder.
* Debian KDE/X11: agent previously observed multi-file export with a successful report. The user completed whole-area export, repeated-export conflict choices, and the Filename translations defaults (`/→_`, `:→~`, `\→^`), confirming the checks pass. Filesystem verification found both correct files in `0`, an empty `1`, and the unchanged source. The package uses Qt fallback dialogs, not KDE portal dialogs.

### Windows reduced-privilege launch

The initial VM session uses an elevated account. A separate test instance of the unchanged package was launched with the Administrators SID marked deny-only, maximum privileges disabled, and medium integrity. The process token was independently inspected (SID attributes `16`, integrity RID `8192`). Native open/list and an export to the user's temporary directory pass; the exported 512-byte file matches its oracle and the source hash is unchanged.

This is reduced-admin-token validation, not a separate standard-user-account login. Windows' `TokenElevation` field still reports `1`; it is not used as evidence that this manually reduced token is a normal UAC login token. The exact launcher and token/export diagnostic are included with the evidence.

An initial test token retained administrator-owned default object permissions, causing process containment to fail. Giving newly created objects the normal user-owned default permissions resolved it with the same application binary. The corrected launcher, token owner/default-DACL settings, process ID, and export evidence are recorded. No application change or VM security-setting change was made. Temporary UTM F10/Tab forwarding shortcuts were removed after testing.

The user's manual confirmation is preserved in `manual-confirmation.json`; automated filesystem evidence is in `windows-native-followup.json` and `linux-native-followup.json`. There are no remaining items in the agreed native checklist.

## Scope and limits

ARM64 only. No x86-64 package validation. A separate standard-user-account login was not tested; Windows permissions coverage uses the reduced-admin token described above. macOS execution tested on 27.2 only (build target 14.4 is not an execution guarantee); package is ad-hoc signed, not notarized. Debian package was tested on Debian 13/glibc 2.41, not arbitrary older Linux distributions. No new forced-termination-at-publication test was added. Packaging metadata still identifies a review prototype; release identification/signing should be settled before public distribution.

Evidence: [validation/v0.2-final](validation/v0.2-final), especially the Qt/broker logs, per-platform relocation JSON, binary audits, final-track reports, and `native-desktop-checks.json`.
