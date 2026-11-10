# v0.2 batch and whole-user-area export — local review

Functional source commit: `7a5995d`, based on merged PR #28 (`c19b440`). No push, pull request, GitHub issue update or release publication is part of this local review.

## Included behavior

- Multiple selected files export directly into one chosen destination directory. A single selected file retains its existing Save dialog workflow.
- Export user area exports all files in the current area into its decimal-numbered child directory, independent of selection. Empty-area export creates the numbered folder explicitly.
- File menu, file/area context menus and labeled buttons expose the shared export actions. The last chosen destination is remembered. Source filename translation settings continue to apply.
- A separate, side-effect-free planner validates every name, translated collision and existing destination before output begins. Collision comparisons are case-insensitive on all platforms. Existing outputs offer Skip existing, Choose another destination or Cancel; no overwrite mode is added. Numbered destinations must be ordinary directories, not files or symbolic links.
- A sequential GUI coordinator reuses the existing broker's single-file export and atomic no-replacement publication. No new disk-image writer, batch broker protocol or helper ABI is introduced.
- Progress and Cancel operate across the batch. Completed files are kept. Failed exports stop the batch, with remaining files clearly not attempted. A detailed report records each filename, destination and outcome plus counts. Existing destinations created after preflight still fail without replacement.
- Source-image fingerprints are checked through the broker for each export. Image bytes, directory metadata and CP/M record padding remain unchanged by export.

The restart regression found and fixed a queued-callback race: cancelling between files and immediately starting another batch could schedule two exports. The queue now refuses to dispatch while a request is in flight. The test includes immediate restart, a post-preflight destination race and a source change after the first completed file.

## Validation

| Suite | macOS ARM64 | Debian 13 ARM64 | Windows 11 ARM64 |
|---|---|---|---|
| Qt UI results | 25 passed | 25 passed | 23 passed, 2 expected skips |
| Broker checks | 11 passed | 11 passed | 9 passed, 2 expected skips |
| Final-track cases | 6/6 passed | 6/6 passed | 6/6 passed |

macOS and Debian exercise cancellation and close during an intentionally interrupted extraction, verifying completed files remain, the private partial file is removed, and later files are not attempted. Those two tests use a POSIX executable-script fault fixture and are skipped on Windows. Windows covers ordinary batch cancellation between files, immediate restart, the existing Job Object process-tree tests, and the other batch workflows. Its two broker skips are also POSIX-specific.

New tests cover selected subsets versus the full area, byte hashes for all three independent fixture files, mixed and all-existing skips, portable case-insensitive conflict handling, unsafe/ambiguous translations, numbered areas 0 and 7, an explicit empty area 1, remembered destination and the folder/conflict dialogs. Existing listing, export, filename/settings, navigation, sorting, image replacement, malformed-reader and deadline tests remain passing. All six final-track helper and broker cases retain their source bytes on all platforms.

Native macOS inspection separately opened the three-file navigation fixture, used Shift selection to export all three files, and exported the whole area into `0/` through the native folder chooser. Each whole-area output matched its selected-file output byte for byte; the source SHA-256 remained `89d246883f82245e633015d41f80be87414e786952b5c6872bbbe243b183419e`. The report visibly listed all three successful exports. The updated developer preview is running with that report open.

Windows used native MSVC 19.44.35229 and Qt 6.12.0. Debian used GCC 14.2.0 and Qt 6.12.0. macOS used the existing private Qt 6.12.0 build with its previously qualified Cocoa accessibility patch. Windows/Linux source and build directories were isolated from earlier validation runs. No VM network access, new software or license acceptance was needed.

Logs and final-track checksums are retained in `docs/validation/v0.2-batch-export/`. Local path prefixes and trailing whitespace were normalized. These GUI suites run offscreen; Windows/Linux native desktop inspection, full package relocation/requalification and x64 validation were not performed. Cancellation deliberately during final destination publication/forced broker termination was not fault-injected; the existing atomic publication code is reused unchanged.

## Review preview and scope

`/private/tmp/CPMImage Filename Review.app` now contains the batch-export build; its existing app name was retained. It is a developer preview linked to this checkout, not a redistributable package. The prior running preview was closed before replacement, so a second old preview was not added.

This implements the batch-export and whole-area-export portion of v0.2. The previously agreed compact toolbar/image-information cleanup and final overall milestone qualification should be reviewed separately before declaring the milestone complete. Tabs, viewer, dual-pane layout and image writes are not included.
