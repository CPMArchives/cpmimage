# CP/M Image — Development Roadmap

**Status:** Approved development sequence, reconciled with Engineering Specification 1.0  
**Project:** CPMArchives/cpmimage  
**Target release:** 1.0  
**Date:** November 14, 2026

This roadmap defines the implementation sequence and acceptance criteria for CP/M Image 1.0. The [Engineering Specification](ENGINEERING_SPEC.md) is authoritative for product behavior, architecture, compatibility, and scope. Milestone assignments do not authorize changes outside their stated scope.

## Baseline — v0.2

The existing Qt 6 Widgets/C++17 GUI, Python broker, isolated native helper, pinned cpmtools 2.23, and LibDsk 1.4.2 form the development baseline. Existing read-only image browsing, limited formats/containers, ARM64 validation, and the odd-track correction are to be preserved unless changes are justified and reviewed.

The current five-format restriction is an implementation limitation, **not** a product requirement.

## v0.3 — Full diskdefs support and format selection

**Priority:** Immediate. This milestone is foundational and must not be deferred.

### v0.3.1 — Backend (internal checkpoint)

- Remove the application-maintained five-format whitelist.
- Resolve arbitrary valid disk definitions through the authoritative cpmtools-derived parser, without a duplicate filesystem parser.
- Preserve original diskdefs identifiers and all functionally significant geometry, translation, and LibDsk parameters.
- Return structured errors for invalid definitions and unsupported backend/container capabilities.

**Acceptance:** Definitions with differing geometry, allocation, and skew parameters work without format-specific application code; existing supported formats remain functional.

### v0.3.2 — Catalogue (internal checkpoint)

- Bundle the complete cpmtools `diskdefs` catalogue.
- Support user-selected external, custom, overridden, and deliberately reduced catalogues.
- Provide metadata for manufacturer/model grouping, system-family categories (including BetterCP/M where appropriate), and unclassified formats.
- Discover and load image-specific `.FDF` files with matching filename stems; accept standalone cpmtools-compatible definitions.
- Support `/diskdefs=PATH` as the global catalogue-selection option for GUI invocation; reject invalid catalogues explicitly.

**Acceptance:** The catalogue can be changed without modifying application code; a matching valid `.FDF` is recognized; custom definitions can be supplied independently of the bundled catalogue.

### v0.3.3 — GUI (internal checkpoint)

- Provide searchable, alphabetized, manufacturer/model/system-family format selection.
- Provide persistent favorites and recent-image format choices.
- Allow format switching without closing the source image.
- Establish format-selection precedence: explicit `/f`, matching image `.FDF`, remembered format, then assisted detection/selection. Rank plausible candidates by container, geometry, and directory evidence; preselect the highest-ranked candidate and allow manual selection from the complete catalogue. Use deterministic ties and require confirmation before writable operations on unfamiliar images.
- Preserve the Save / Discard / Cancel policy for format changes with unsaved work as writing support becomes available.

**Acceptance:** A user can choose any backend-supported definition, open and reinterpret images without closing them, and use custom/reduced catalogues through the GUI.

**Release policy:** v0.3.1–v0.3.3 are independently testable development checkpoints; they need not be published as three separate releases. Stop for maintainer review after the complete v0.3 milestone.

## v0.4 — Safe writing and file management

- Introduce private mutable working images, dirty-state tracking, Discard, Save, and Save As.
- Implement safe output publication and appropriate conflict, interruption, and concurrent-change handling.
- Design source identity and single-writer ownership using native file identity and locking, including symlink/hard-link aliases, source replacement during publication, crash recovery, and external-change revalidation; avoid indefinite stale lockouts.
- Implement file import/export, rename, confirmed deferred deletion, supported RO/SYS/ARC Properties editing, optional text conversion, timestamp handling where supported, and filename collision safeguards.
- Import valid CP/M filenames automatically; review names requiring 8.3 shortening/translation in one editable batch dialog, detecting within-batch and destination collisions. Use Replace/Skip/Cancel with Skip preselected and Apply to All for batch conflicts.
- Implement single-file F2/context-menu Rename as a metadata-only update across extents; reject rename collisions without Replace. Deletion requires confirmation, changes only the working image, and is reversible through Discard of all unsaved changes (not per-operation Undo).
- Implement same-filesystem cross-user-area moves by updating user bytes across extents, without copying file data; preserve metadata and enforce destination collision checks.
- Support independent export, import, and replacement of reserved system-track bytes on existing images. Use the selected disk definition to locate the region, protect filesystem data, validate size, and define short-input padding. The user, not CP/M Image, determines whether supplied system software is bootable or appropriate.
- Generate or update an image-specific, standalone cpmtools-compatible `.FDF` on explicit Save or Save As by default, including an otherwise unmodified image; never create it merely on open, detection, or format selection. Provide a preference to disable automatic creation.
- Detect conflicting existing sidecars and define safe image-plus-sidecar publication/recovery behavior.
- Treat `.FDF` as a record of the user-selected interpretation, not proof of the underlying disk format or a filesystem conversion. Implement a Confirm Disk Definition warning on first sidecar creation and when replacing a sidecar with a different definition; allow persistent suppression of routine warnings, but never suppress detected integrity warnings. Cancel must abort Save without publication.

**Acceptance:** Edited images reopen correctly; unrelated contents and source images remain protected on failure; system-track bytes round-trip accurately; generated `.FDF` files can be parsed by independent unmodified cpmtools; first-save/changed-definition warnings, suppression preference, and cancellation behave as specified.

## v0.5 — Container interoperability

- Implement read/create/modify/save workflows for IMD, DMK, and JV3 alongside RAW.
- Evaluate the pinned LibDsk drivers first for formats they already support; investigate DMK separately.
- Decode containers to the **RAW layout expected by unmodified cpmtools** using the selected disk definition, not merely numeric sector-ID order. Apply sector translation/skew exactly once.
- Implement reverse mapping for container encoding and qualify emulator-compatible output.
- Provide IMD comment viewing, editing, and preservation in Image Information.
- Report missing/ambiguous sectors and conversion limitations; never silently invent or discard essential data.
- Expose known image geometry where useful.

**Acceptance:** Independent unmodified cpmtools can read generated RAW images with the corresponding `diskdefs` entry and extract correct file contents; IMD/DMK/JV3 conversions pass suitable round-trip and emulator checks; comments survive save/reopen.

## v0.6 — Image creation and formatting

- Create new images and format existing images using the same format engine and selected disk definition.
- Support applicable CP/M Plus labels and timestamp initialization.
- Reuse the system-track binary import mechanism for optional boot/system-track content.
- Require confirmation for destructive formatting and retain the private-working-image/save model.

**Acceptance:** Newly created and formatted RAW images are independently accessible by unmodified cpmtools using the corresponding definition; formatting does not alter the original source until Save.

## v0.7 — Multiple images and desktop transfers

- Support tabbed image sessions within each window and multiple independent application instances, with per-tab format, user area, and unsaved state. Allow shared read-only access but enforce a single writable owner per source, with external-change checks before save.
- Integrate cross-instance ownership: subsequent opens of an already writable-owned source use read-only access; Save As transfers writable ownership to the new destination only after safe publication, with old ownership released afterward. Test aliases, failed acquisition, and crash recovery.
- Support copying/moving files between images and user areas.
- Implement the required Show All virtual view across user areas, retaining source user identities in selections and prompting for destination user areas when importing or pasting.
- Implement default flat multi-area export and native drag-out, consolidating byte-identical same-name files and checking existing destination contents; offer Overwrite, Skip (default), Rename, or Cancel for differing-content collisions. Cancel stops the remainder; completed exports remain; report skipped and deduplicated files.
- Implement drag-and-drop, clipboard transfers, multiple selection, and conflict handling. Copy/Paste duplicates; Cut/Paste moves only after successful destination writing; drag-and-drop copies by default between user areas/images and requires an explicit action to move. Cross-instance transfers use native host clipboard/drag-and-drop without a custom interprocess protocol.
- Preserve the narrow left CP/M user-area sidebar (approximately 90–120 pixels by default, resizable) and wide right file-list pane; do not add a permanent host filesystem browser.
- Keep native drag-out temporary files available through asynchronous destination reads, including cancellation and failures. Use platform-appropriate cleanup and preserve the read-only source.
- Support native host file-manager drag-and-drop: drop images to open, host files onto user areas to import, CP/M files onto host folders to export, and files between open images to transfer. Distinguish opening an image from importing it as data by drop target.
- Write or update Help topics as features are implemented, using repository Markdown as the common source for offline and online Help.
- Integrate recent images and pinned image shortcuts.

**Acceptance:** Cross-image transfers preserve contents, handle partial failures safely, and do not lose source data before successful destination writes.

## v0.8 — Integration, command line, and usability

- Support GUI command-line image opening with `/img=PATH`, persistent `/f=FORMAT` for subsequent images, and global `/diskdefs=PATH`; provide conventional long-option equivalents.
- Implement a headless dispatcher for the **complete bundled cpmtools utility suite**, using original utility names and syntax, including `/cpmcp`, `/cpmchattr`, `/cpmls`, `/fsck.cpm`, and `/fsed.cpm`.
- A recognized utility selector **anywhere** on the command line always prevents GUI initialization. Multiple selectors or incompatible GUI-only options cause explicit errors, never GUI launch.
- Build and package the pinned standalone cpmtools executables for each supported host platform. Preserve arguments, stdin/stdout/stderr, and exit codes, with explicit Windows console qualification.
- Extend candidate discovery and evidence thresholds beyond the v0.3 checks.
- Complete preferences, Image Information, progress/error presentation, and general workflow integration.
- Finalize the narrow-sidebar file list with default Filename, Records, Size (bytes), Attributes columns; optional User and supported timestamps; numeric sorting, column-header Choose Columns, reorder/resize, and persistent layout. Display logical bytes as records × 128, not exact original length.
- Add All above numbered user areas, automatically show User in Show All and restore the normal column configuration when leaving it; implement View → Show All User Areas and Ctrl/Cmd+Shift+A as a per-tab toggle returning to the previously selected user area and clearing selections on view switches.
- Provide the five-button labeled toolbar (Open, New, Save | Import, Export), visible by default and persistently hideable via View → Toolbar. Keep sorting and other contextual commands off the toolbar.
- Implement target-specific context menus for files, user areas, empty file-list space, image tabs, and column headers, with selection-sensitive availability and consistent underlying actions. Double-click opens Properties in 1.0; F2 renames; Delete confirms; Properties edits RO/SYS/ARC and shows available metadata.
- Implement approved function keys: F1 Help, F2 Rename, F4 Properties, F5 Refresh, F6 Move to User; reserve F3 for a future viewer, leave F7–F9/F11–F12 unassigned, and respect native F10 menu behavior.
- Implement Ctrl/Cmd+O/S/C/X/V/A and Delete with native platform modifier conventions, state-sensitive availability, shared command handlers, and deletion confirmation.
- Integrate bundled searchable offline Help as the primary Help experience, accessible through the Help menu and F1 where appropriate, with context-sensitive entry points.
- Provide a Help-menu link to companion online documentation generated from the same repository Markdown source.
- Review native desktop visual consistency and refine the approved narrow-sidebar/right-file-list layout without redesigning the application.

**Acceptance:** GUI image/format arguments behave predictably; headless commands match the corresponding bundled utilities; the GUI never opens for utility dispatch, regardless of argument order. Providing existing `fsck.cpm` and `fsed.cpm` executables does **not** add GUI checker/repair/editor features.

## v0.9 — Qualification and packaging

- Run filesystem, geometry, sector-ordering, RAW interoperability, container, sidecar, system-track, and cross-platform regressions.
- Validate source preservation, interruption/recovery, error handling, CLI output/exit behavior, and package relocation.
- Establish and test the supported OS/CPU release matrix. Existing ARM64 macOS/Windows/Linux evidence is a starting point, not proof of x86-64 compatibility.
- Produce self-contained packages with required runtime dependencies, cpmtools executables, licensing, and corresponding-source materials.
- Publish online Help and complete documentation and release-readiness checks.
- Validate offline Help without network access, search, contextual topics, online/offline content consistency, and Help links.
- Qualify native UI appearance, sidebar sizing, column sorting/persistence, toolbar visibility, context menus, Properties, function-key and standard shortcuts (including macOS/Windows/Linux differences), tab/instance ownership, conflict behavior, safe rename/delete, filename review, and bidirectional drag-and-drop with host file managers on the release platforms.
- Qualify platform-specific locking and file-identity behavior across aliases, independent processes, safe replacement, Save As ownership transition, stale locks, and noncooperating external modifications.
- Qualify asynchronous native drag-out consumption and cleanup across release platforms.
- Qualify Show All's cross-user-area listing, keyboard toggle, source identity preservation, destination prompts, flat export, checksum/byte-comparison deduplication, host-existing-file comparisons, Rename conflicts, Skip/Cancel semantics, and native drag-out behavior.

**Acceptance:** Release targets have reproducible builds, qualified installers/packages, working bundled offline Help, published online documentation, verified native file-manager transfers, and documented evidence of the required workflows.

## v1.0 — Release

Publish the completed, qualified cross-platform CP/M disk-image file manager against the approved Engineering Specification.

The initial release is focused on filesystem and image management, not disk preservation or forensic editing.

## Explicitly deferred beyond v1.0

- Graphical `fsck.cpm`-style consistency checking and repair.
- Graphical `fsed.cpm`-style filesystem structure/sector editing.
- Low-level track/sector and flux editing.
- Advanced disk-image comparison, merging, and reconstruction.
- Physical floppy-drive access.

The existing cpmtools CLI executables, including `fsck.cpm` and `fsed.cpm`, remain accessible in v1.0 headless mode.

## Engineering controls

- Maintain the Qt GUI → Python broker → isolated native helper architecture unless a consequential change is reviewed and approved.
- Use cpmtools as the authoritative filesystem engine and `diskdefs` interpreter; do not add an application-level format whitelist.
- Treat RAW compatibility with **independent, unmodified cpmtools** as the container interoperability invariant.
- Keep source images safe until explicit publication.
- Test and review each milestone independently. Do not interpret this roadmap as authorization to implement all milestones in a single Work assignment.
- Resolve open implementation details in the milestone where they matter, without reopening approved product requirements.
- Treat the approved 1.0 product/UI design as feature-frozen; **Show All User Areas is required** for 1.0, while the future file viewer is not a release blocker.
