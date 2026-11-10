# v0.2 uppercase filenames and translation settings — local review

## Proposed commit

Keep CP/M names uppercase and configure export translations.

This follows the merged navigation slice. No image writes, import, batch export, GitHub issue updates or release publication are included.

## Behavior

- Read-only broker identities are uppercase, including their user-area prefix. Lowercase caller input is accepted and normalized at the adapter boundary. The experimental write adapter retains its existing contract.
- Display and suggested single-file export names are uppercase. Suggestions retain the filename and extension without adding a user-area prefix: `-CATALOG.310` remains `-CATALOG.310`.
- Settings → Filename translations (Command+, on macOS) supports adding, editing, removing, restoring defaults, saving and cancelling mappings, with a live example. Settings persist through QSettings. Invalid saved data falls back to defaults with a visible notice.
- The three default mappings are `/ → _`, `: → ~`, and `\ → ^`, applied once per original character without cascading. `SIG/M.LIB` is displayed unchanged and suggests `SIG_M.LIB` for export.
- Duplicate source mappings and unsafe replacement characters are rejected. Export suggestions refuse translated collisions within the current user area, using case-insensitive comparison, and reject unsafe host names and Windows device basenames. Existing destinations are never overwritten.
- `$`, backtick and `+` are unchanged by default. Pipe and double quote require a user-defined mapping before the GUI can suggest their export. Plus remains unresolved for classic DOS interchange: this does not promise universal DOS-compatible output. Reverse import translation and collision-free round trips remain future work.
- The helper supports colon as opaque directory-name data using a separate read-only cpmtools object. Existing CLI validation is retained. The patch, build recipes and generated/static modification notices are included.

## Image replacement crash

The macOS crash report ended in `QTableModel::removeRows` during `Window::resetImage`. Reset now suppresses table selection callbacks, clears selection/current item before deleting rows, and disables sorting during removal. This prevents selection UI updates from running during the partial reset. The report identifies the failing location; the precise Qt-level reentrancy was not captured under a debugger.

An automated regression repeatedly replaces selected/sorted populated images with empty images and verifies no selection callback during deletion. Native macOS testing switched SIGMV309 → SIGMV310 → SIGMV309 through Open with selections present. The source copies match the originals. The owner subsequently confirmed the preview is working. Two older running previews were closed to avoid testing stale code.

## Validation

macOS ARM64, Qt 6.12.0 with the previously documented Cocoa accessibility patch:

- UI suite: 19 passed, zero failures/skips, including settings persistence/cancel, mapping validation/collision detection, uppercase identities/display/save-dialog suggestion, repeated image replacement, and prior selection, broker-failure and process-tree checks.
- Broker integration: all 11 tests passed. Synthetic directory names exercise slash, colon, backslash, pipe, double quote, dollar, plus and `-CATALOG.310`; extracted bytes match the independent fixture SHA-256 and images remain unchanged.
- All six final-track helper/broker regression cases passed with sources unchanged.
- Native macOS settings dialog and image replacement were checked separately from offscreen automated tests. Owner tested uppercase retention and confirmed the current preview works.
- Build recipe shell syntax and diff whitespace checks passed. Notice generator syntax checked.

Evidence: `docs/validation/v0.2-filenames/`. Windows ARM64 and Linux ARM64 validation is now complete as recorded below. Native Windows/Linux desktop interaction, full relocatable package validation and x64 validation remain outstanding.

## Review preview

`/private/tmp/CPMImage Filename Review.app` is the current developer preview. It uses the existing private Qt runtime with the Cocoa patch and the rebuilt helper, and links to this checkout. It is not a distributable release. No push is authorized by this preparation step.

## Windows and Debian validation follow-up

Both existing offline UTM VMs tested source commit `8dba5db` in new isolated directories. SHA-256 verification checked all 329 source files with no mismatches on either VM. The original source trees and older qualified helpers were preserved.

| Check | Windows 11 ARM64 | Debian 13 ARM64 |
|---|---|---|
| Native Qt application build | MSVC 19.44.35229 / Qt 6.12.0 | GCC 14.2.0 / Qt 6.12.0 |
| Qt UI suite | 19 passed, zero failures/skips | 19 passed, zero failures/skips |
| Broker integration | 9 passed, 2 expected POSIX-only skips | 11 passed |
| Final-track regression | 6/6 helper and broker cases passed | 6/6 helper and broker cases passed |
| Source unchanged checks | Passed | Passed |

The UI tests run offscreen and cover saved translations, cancellation, name collisions, uppercase model/display/export suggestions and repeated selected/sorted image replacement. They do not replace native desktop visual checks. Offscreen plugin/font/collation warnings were nonfatal and are retained in the logs.

The helper was rebuilt for each platform: GCC with the existing libdsk installation on Debian, LLVM-MinGW ARM64 cross-compilation on macOS for Windows, then execution inside Windows. The Windows Qt application and tests were built natively with MSVC, including its icon resource. Expanded filename integration exercises colon, slash, backslash, pipe, quote, dollar, plus and catalogue names, with independent expected export hashes. All six geometry/final-track cases also pass and retain source bytes.

The Windows helper build recipe needed one local fix: libdsk's old `config.guess`/`config.sub` rejected ARM64. The recipe now copies the newer scripts from the pinned cpmtools archive before configuring libdsk, matching the existing Linux approach. The helper then rebuilt and passed the Windows suite. This build-script correction is in addition to the tested application source commit; the host helper build log records the actual compilation.

The first Windows invocation failed in the guest command wrapper before configuration. A direct Python subprocess runner with the same installed toolchain completed validation. Its initial compiler check needed the existing recipe's `CMAKE_TRY_COMPILE_CONFIGURATION=Release`; with that setting the native build succeeded. No account, network access, download or new license acceptance was needed.

Evidence is retained in `docs/validation/v0.2-filenames/`, including configure/build output, complete test logs, source integrity manifests, helper hashes and final-track export checksums. Local path prefixes and trailing whitespace were normalized. No GitHub changes or push were made.
