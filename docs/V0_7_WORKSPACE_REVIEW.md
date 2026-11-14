# v0.7 workspace review

Tabs retain independent working images and brokers. The active tab supplies the menu actions. Recent and pinned images open through the ordinary image inspection path.

Cut transfers within one application process use native file URLs for the payload and retain the original session and revision in memory. A successful destination import authorizes removal only while that source session and revision still match. File URLs received by another process carry the payload but provide no validated acknowledgment of broker import, so pasting there copies and retains the source.

Successful drag and clipboard staging remains available in the application cache to serve delayed reads. Subsequent transfers prune staging directories older than seven days. Cancelled drags and failed staging release their temporary directories. Native Finder delayed consumption still needs a manual check.

The macOS ARM64 dirty-tab check covers Save, Discard, and Cancel. A two-broker failure test verifies that a disk-full import leaves both source content and the destination directory intact.

Remaining acceptance work includes verified cross-instance moves, native delayed drag reads, explicit drag move handling, and coverage of the transfer conflict and cancellation paths.

## Follow-up

Shift-drop within the same process requests a move through the guarded destination-import path. Ordinary drops copy. The full macOS ARM64 Qt suite passed after this change, including dirty-tab Save, Discard, and Cancel. The focused disk-full broker test also passed. The suite does not drive the native Finder or assert the Shift-drop gesture end to end.

Cross-instance Cut/Paste still needs a verified receipt. Native file URLs alone do not communicate which files a destination skipped or successfully imported. A possible extension is to exchange a single-use transfer token and per-file receipt through the native clipboard. This is a proposed application-level protocol and requires approval under the current transfer design.
