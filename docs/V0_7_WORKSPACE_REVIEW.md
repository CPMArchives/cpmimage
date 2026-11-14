# v0.7 workspace review

Tabs retain independent working images and brokers. The active tab supplies the menu actions. Recent and pinned images open through the ordinary image inspection path. Flat export consolidates identical output files.

Cut transfers use native file URLs. Across application processes, a single-use clipboard token and per-file import receipt identify which files were successfully imported. Source removal requires confirmation and a matching source session and revision. Skip, Cancel, and failed imports retain the affected source files. Ordinary drops copy. Shift-drop within one process requests a move through the guarded import path.

The file view now stages exports before returning from its drag-start handler. Previously it attempted to start the native drag from an asynchronous completion callback after returning from that handler. Staged files offered to the native drag service remain in the application cache even if the platform reports that the gesture was ignored. Subsequent transfers prune staging directories older than seven days. Failed staging still releases its temporary directory.

## macOS ARM64 validation

- The full Qt suite passed with the rebuilt app, including dirty-tab Save, Discard, and Cancel.
- Native two-instance Cut/Paste passed for Skip, Replace, Cancel, and disk-full failure. The disk-full destination retained its original file, the source retained the offered file, and no source-removal prompt appeared. The independent two-broker disk-full check also passed.
- A manual drop from the rebuilt app produced `BINARY.BIN` in Finder. After the gesture, both the Finder copy and the retained staged payload matched all 512 bytes extracted independently with `cpmcp`. SHA-256 was `110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b`. The source RAW image was unchanged. The test verified post-drop availability but did not measure when Finder first opened the staged file.
- Native Shift-drop was not exercised end to end. Windows and Linux native file-manager behavior was not tested in this pass.
