# v0.2 compact toolbar and image information — local review

Functional commit: `578031a`, based on merged PR #29 (`a856809`). Local review only: no push, pull request, issue updates or release publication.

## Changes

The primary controls are Open image, Export selected and Export user area, with a short Read-only indicator. The layout selector and secondary Image information button are on a separate row, avoiding the previous wide combined row. Large introductory text and permanent technical footer were removed. The browser shows only the filename, also used in the window title; its tooltip retains the full path. Cancel is visible only while busy. Batch progress is hidden after completion, and Export report appears only when a report is available, beside the normal status text.

Image information is available from its labeled button and the File menu. It shows the full source path, layout name/format identifier, container, total file count and populated-area count, snapshot SHA-256, E5-tail normalization details, read-only/manual-selection/CP/M-padding notes, and the last completed export's byte count and SHA-256. These are selectable plain text. Export-success status now gives the byte count without the hash. Image reset clears cached diagnostic data; controls are disabled when no valid session exists or while busy.

Existing open/export shortcuts, translation settings, navigation, selection, context menus, batch reports, cancellation and the read-only broker/helper are retained. This change does not alter the broker protocol, backend implementation, dependency set or image formats.

## Validation

| Suite | macOS ARM64 | Debian 13 ARM64 | Windows 11 ARM64 |
|---|---|---|---|
| Qt UI results | 26 passed | 26 passed | 24 passed, 2 expected skips |
| Broker tests | 11 passed | 11 passed | 9 passed, 2 expected skips |
| Final-track cases | 6/6 passed | 6/6 passed | 6/6 passed |

The new test checks a minimum width at or below the 920-point review window, primary/dialog state before opening and while busy, filename/window-title presentation, absence of SHA-256 from main labels and export-success status, dialog access through button and menu, all-area counts, the independently calculated source fingerprint, last-export fingerprint, trimmed-tail details, and clearing stale information on image replacement, layout changes and failed opens. Existing batch, cancellation, filename/settings, sorting, source-preservation and broker-failure tests remain passing.

The first Windows run found a test-only path-separator mismatch: the expected path combined backslashes and slashes while QFileInfo normalized the dialog's path. The assertion now compares the normalized absolute path. The complete suite then passed on all three platforms; no application change was needed for this mismatch.

The two Windows UI skips are the pre-existing POSIX slow-helper fixtures for cancel/close during extraction. Windows still exercises ordinary batch cancellation and Job Object process-tree cases. Its two broker skips are also POSIX-only. Full in-flight extraction cancellation remains tested on macOS/Linux.

Native macOS inspection opened SIGMV310.DSK through the file dialog, visually checked the compact controls and expanded browsing space, then opened Image information. The dialog showed six files, one populated user area, the matching snapshot hash and the temporary E5-tail note. The main view displayed the filename and selection summary without the snapshot hash or normalization text. The updated preview is running with Image information open.

Windows used native MSVC 19.44.35229 and Qt 6.12.0; Debian used GCC 14.2.0 and Qt 6.12.0. macOS used the existing private Qt 6.12.0 build with its qualified Cocoa accessibility patch. Windows/Linux ran in separate source/build directories inside the existing offline UTM VMs. No download, network access or new license acceptance was needed.

Evidence is retained in `docs/validation/v0.2-toolbar-image-info/`. Local path prefixes and trailing whitespace were normalized. The GUI suites run offscreen; native Windows/Linux desktop inspection, full relocatable-package qualification and x64 checks were not repeated. All six final-track reports confirm successful helper/broker exports with source bytes unchanged.

## Review preview

`/private/tmp/CPMImage Filename Review.app` contains the updated developer build, retaining its existing app name. The previous preview was closed and its exit verified before launching this version. It links to the current checkout and existing runtime/helper; it is not a redistributable package. Final overall v0.2 qualification remains a separate review step.
