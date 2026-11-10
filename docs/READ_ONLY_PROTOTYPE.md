# Read-only prototype

The merged v0.1 prototype is the baseline. v0.2 adds user-area navigation, multiple selection and configurable filename translations. Batch and whole-user-area export are now merged. The local toolbar review compacts the controls and moves technical details into Image information; see [the review and validation scope](V0_2_TOOLBAR_REVIEW.md).

## Runtime boundary

Qt Widgets (`prototype/src`) sends bounded JSON requests to `prototype/broker.py`, launched with isolated Python flags. The broker retains a private image snapshot and invokes the native read-only helper. The helper links pinned cpmtools filesystem/device objects and libdsk. `experiments/process-comparison/adapters.py` is the qualified parent contract retained from the integration experiment; the application's adapter is explicitly read-only. Historical CLI/mutation experiment branches in that module are not exposed by the GUI/broker, and the shipped helper is compiled with `CPMIMAGE_READ_ONLY`.

The GUI and broker reject operations outside listing/export. Existing export destinations are never overwritten. Source hashes guard against a changed image after opening. Exported bytes retain CP/M 128-byte record padding. Source-change checking is not an exclusive file lease or a filesystem transaction.

POSIX process sessions and the helper guard contain child processes; Windows uses kill-on-close Job Objects. GUI deadlines, cancellation grace periods, response bounds, schema checks, and broker-restart tests cover failure recovery. No image file is edited by the application.

## Explicit supported layouts

| Selection | Container |
|---|---|
| IBM 3740 | raw |
| BetterCP/M CPMSIM layout | raw |
| TRS-80 Model II layout (`bcm2ss`) | raw |
| BetterCP/M Model 4 layout | raw |
| Ampro Little Board layout | raw |
| Ampro Little Board backend test case | standard CPCEMU DSK |

The last structured-container case is a tested backend combination; it is not currently a separate GUI format choice. A `.DSK` extension does not identify a container. No automatic detection, arbitrary diskdefs selection, compressed containers, physical disks, or unsupported format combinations are promised.

Short IBM3740 raw images are accepted only when sector-aligned and every directory and allocated sector is present. Only an unallocated missing tail is filled with E5 in a temporary snapshot. The original length/hash are retained. Missing file or directory sectors are rejected.

## Final-track correction

`packaging/source/cpmtools-2.23-odd-tracks-libdsk.patch` corrects cpmtools' libdsk bridge. When probing does not match the requested layout, the old `tracks/2` fallback loses one track from odd track counts. The patch keeps odd counts single-sided, preserving all tracks. Explicit geometry and successful probes retain precedence; even-count fallback behavior is unchanged. libdsk itself is not modified. Build scripts apply the recorded, dated patch before compiling the bridge object.

This does not add general geometry detection or settle a long-term public backend API.

## Browsing and export

The v0.2 UI supports one image, a resizable left pane containing all user areas 0–15, and a sortable Name/Bytes/Attributes table filtered to the selected area. Populated areas have a centered plus on their folder icon. Selection count/byte totals are visible. Changing layout clears the session, listing, selection and populated markers.

The primary toolbar contains Open image, Export selected and Export user area, with a Read-only indicator. The layout selector and Image information button occupy a separate row. The filename appears above the browser and in the window title; the full path is also available as its tooltip. Cancel appears only during an operation, batch progress appears during transfer, and Export report is shown when a report is available.

**Image information…**, also available from File, contains the full source path, selected layout and identifiers, container, total file/populated-area counts, snapshot SHA-256, temporary E5-tail normalization note, read-only/manual-format/record-padding details and the last completed export's byte count and hash. The dialog is selectable plain text, unavailable until an image is open and while operations run. Image reset clears its cached snapshot/export details. Technical hashes and normalization details no longer occupy the everyday browser or export-success status.

CP/M names remain uppercase. Single-file export suggests the displayed filename without a user-area prefix. Configurable translations default to `/ → _`, `: → ~` and `\ → ^` on every host OS. Source names are displayed unchanged; translation applies only to export suggestions. No reverse import mapping is implemented. See [filename policy](FILENAME_POLICY_NOTES.md).

For multiple selected files, **Export selected…** asks for a destination folder once and exports directly into it. **Export user area…** exports every file in the current area, regardless of selection, into a decimal-numbered child folder such as `7/`. Explicit empty-area export creates its empty numbered folder. Both actions are available from File, the labeled buttons and the corresponding file/folder context menus.

Preflight checks all planned translated names before creating files or a numbered folder. Unsafe names and case-insensitive translation collisions are refused. Existing destinations offer **Skip existing**, **Choose another destination** or **Cancel**. Existing numbered folders can be reused; a numbered destination that is a file or symbolic link is refused. A destination created after preflight is still never overwritten by the broker. The last accepted destination is remembered.

Files export sequentially with progress and Cancel. Completed exports are retained on cancellation. Interrupted extraction stays in private staging and is cleaned up through the existing adapter. A failure stops the batch and marks remaining entries not attempted; a later export can skip completed destinations. **Export report…** gives counts and a per-file exported/skipped/failed/cancelled/not-attempted result. Reports are retained in memory for the current application run.

Tabs, automatic format detection, arbitrary diskdefs, image mutation, and future import/creation/drag-and-drop workflows remain outside this implementation. Full binary-package requalification and native Windows/Linux desktop checks for this slice remain outstanding.
