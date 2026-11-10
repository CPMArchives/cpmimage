# v0.2 navigation and selection — local review

## Scope

- All 16 user areas, including empty areas, in a flat folder list with a resizable divider. Populated folders have a centered plus and accessible file-count descriptions.
- The file table shows only the selected area, with sortable Name, numeric Bytes and Attributes columns. Area 0 opens initially. Switching areas clears the old selection and selects the first file when present.
- Shift ranges, platform Ctrl/Command individual selection, and Select All. Current area, selected count and byte totals are visible.
- Single-file export remains available only with exactly one selected row. Sorting preserves the original backend identity. Multiple selection cannot silently export only the current file.
- Changing layout stops the idle reader and clears the session, rows, selection and populated markers. Controls remain disabled during an operation, as in v0.1.

This is the first implementation slice, not all of v0.2. Filename settings, revised export naming, batch/whole-area export, contextual menus, the compact toolbar and moving diagnostics to image information are still pending. No image write operations are added. This first slice is prepared for a review PR; no image write operations or release publication are included.

## Local validation

Both Homebrew Qt 6.12.0 and the existing private Qt 6.12.0 developer builds compile and pass the complete GUI suite (16 QtTest results including setup/cleanup; 14 functional/data cases). This includes prior listing/export, dialog, malformed-reader, restart and process-tree tests, plus two new workflows.

The new tests exercise all areas, populated versus empty icon rendering, per-area file identities, empty images, layout/session reset, Shift and Ctrl/Command selections, Select All, no-selection/multiple-selection export guards, numeric sorting, correct export after sorting, and unchanged source bytes. Test-only multi-file data is derived into a temporary image from the independent fixture; committed disk images are unchanged.

Native macOS inspection with the patched private Qt runtime verifies opening the fixture through the native file dialog, visible plus/plain folder icons, Command-click two-file selection (1280 bytes), empty-area navigation, and area 15/area 0 keyboard navigation. The selected accessible cells remain queryable. Native ARM64 Windows 11/MSVC 19.44 and Debian 13/GCC 14.2 builds now also pass the full GUI suite with Qt 6.12.0 (16 QtTest results including setup/cleanup). Windows passes nine broker tests with two expected POSIX-only skips; Debian passes all 11. Both pass all six final-track helper/broker cases with source images unchanged. The GUI tests run offscreen; the native macOS inspection above is separate. New Windows/Linux desktop visual inspection and full relocatable-package qualification were not performed in this slice. x64 remains unvalidated.

The tests used isolated source/build directories in the existing offline VMs, their installed native compiler/Qt kits, and the previously validated read-only helper/runtime. No new toolchain download or license acceptance was needed. Windows build output explicitly compiles the icon resource. The transferred GUI, tests, CMake and icon assets match the local checkout byte-for-byte. Logs are in `docs/validation/v0.2-navigation`; local source/kit paths are normalized and trailing whitespace removed. Original VM log timestamps retain their guest time zones.

## Approved app icon

The owner approved replacing “inside” with “image” in the CPMArchives profile artwork. The approved PNG is embedded as the Qt application/window icon; macOS ICNS and Windows ICO/native resources are included. The source provenance, original image, edit prompt and approved output are retained in `prototype/resources`. Native macOS builds and Windows/MSVC resource compilation pass. This does not introduce icon editing or new image-container support into the app.

## Native macOS accessibility dependency

The native check reproduced a crash in Qt's `QMacAccessibilityElement accessibilitySelectedChildren`; offscreen tests did not expose it. The upstream defect is [QTBUG-149612](https://qt-project.atlassian.net/browse/QTBUG-149612). The fix is backported from [Qt's 6.12 commit b74d6cb3c5e8418602494ae549b60839d88ef858](https://code.qt.io/cgit/qt/qtbase.git/commit/?id=b74d6cb3c5e8418602494ae549b60839d88ef858).

The patch prevents native Cocoa accessibility elements from deleting interfaces owned by the accessibility cache/item view. It retains the upstream regression test, but the upstream Qt regression suite was not run locally. The native application check passed with the rebuilt plugin and a consistent private Qt runtime; accessibility was not disabled. Homebrew Qt itself was not modified.

Patch SHA-256: `13580baf69c16fa4559464194a73c38e4174e0a967eb71ee08ec478c512384ea`. The macOS Qt source-build recipe applies it; license-notice generation records it. Corresponding-source archives include the tracked patch automatically. The previously qualified v0.1 source and plugin were restored after using their existing build tree to compile the isolated test plugin. The first preview attempt mixing Homebrew and private Qt was discarded.

## Review preview

The local-only developer preview is `/private/tmp/CPMImage v0.2 Review.app`. It uses the current checkout, read-only helper and developer Python through local links; it is not a relocatable release package. Its native Cocoa plugin carries the above upstream fix. Review patch and notes are provided outside the checkout.
