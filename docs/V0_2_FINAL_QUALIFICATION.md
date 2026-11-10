# v0.2 final qualification review — 2026-11-11

Application source: merged PR #30, `c467c34`. Branch: `work/v0.2-final-qualification`.
No application behavior changes in this proposal. No push, release, or GitHub changes.

## Result

Automated regression and relocated ARM64 package checks pass on macOS, Windows, and Linux. Native desktop smoke checks made useful progress, but **final native desktop sign-off is still open**. Do not close the qualification milestone or label this a fully qualified public release yet.

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
| Native multi-file export output hashes | pass; UI connection lost afterward | not completed | pass, visible report and hashes |

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

## Native observations and remaining checks

* Debian KDE/X11: launched the relocated package from KRunner, opened the synthetic image, saw uppercase `BINARY.BIN` and `SIG/M.LIB`, selected both files and exported them using the real desktop folder dialog. The app reported two exports and no failures; both disk outputs match independent checksum oracles. These are Qt fallback dialogs, not KDE portal dialogs.
* Windows: launched the relocated package through Run and opened the synthetic image through the native Windows chooser. Both uppercase names, the literal slash, file sizes, and populated user-area icon were visible. VM input/focus automation did not reliably reach the table or other controls; native batch/whole-area/conflict workflows remain unverified. Run indicated administrative privileges, so a standard-user desktop launch also remains to be checked.
* macOS: launched the relocated Cocoa package, opened the image through the native chooser, selected both files and selected an export folder. Both outputs were created with correct names and checksums. CUA then timed out on accessibility and screenshot requests, including after reconnect/reset. A process sample found the main thread idle in the Cocoa event loop. That does not prove a crash, nor does it prove post-export responsiveness; a manual report/responsiveness check remains necessary.

Remaining native sign-off: Windows batch/whole-area/conflict dialogs and normal-user launch; Linux whole-area/conflict/settings dialogs; macOS post-export report and continued responsiveness. Existing automated Qt tests cover the underlying workflows, but they do not replace these remaining desktop checks.

## Scope and limits

ARM64 only. No x86-64 package validation. macOS execution tested on 27.2 only (build target 14.4 is not an execution guarantee); package is ad-hoc signed, not notarized. Debian package was tested on Debian 13/glibc 2.41, not arbitrary older Linux distributions. No new forced-termination-at-publication test was added. Packaging metadata still identifies a review prototype; release identification/signing should be settled before public distribution.

Evidence: [validation/v0.2-final](validation/v0.2-final), especially the Qt/broker logs, per-platform relocation JSON, binary audits, final-track reports, and `native-desktop-checks.json`.
