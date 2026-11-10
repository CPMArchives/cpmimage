# Read-only prototype

The merged v0.1 prototype is the baseline. This local v0.2 review adds user-area navigation and multiple selection; other owner feedback remains backlog work. See [the review and validation scope](V0_2_NAVIGATION_REVIEW.md).

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

## Current UI limitations

The v0.2 navigation work supports one image, a resizable left pane containing all user areas 0–15, and a sortable Name/Bytes/Attributes table filtered to the selected area. Populated areas have a centered plus on their folder icon. Multiple selection and selection count/byte totals are supported. Export remains limited to exactly one selected file until the separate batch-export step. The suggested export filename currently uses an internal lowercase name prefixed with its user area; `/` is replaced with `_`. The table displays uppercase names. Filename defaults/settings, batch and whole-area export, tabs, and future import/creation/drag-and-drop workflows remain backlog items.

The image layout selector clears the current listing, selection and populated-area markers and stops the current reader when changed. Some unusual CP/M names remain outside the prototype's allowed naming subset. Dollar signs, plus signs and opaque CP/M slashes are supported by the read-only name validator. No reversible import mapping is implemented.
