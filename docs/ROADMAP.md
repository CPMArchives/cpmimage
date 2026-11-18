# CP/M Image — Development Roadmap

**Status:** Approved development sequence, reconciled with Engineering Specification 1.0  
**Project:** CPMArchives/cpmimage  
**Roadmap through:** v2.0\
**Original 1.0 roadmap date:** November 14, 2026

This roadmap records the implementation sequence and acceptance criteria for CP/M Image 1.0, the subsequent milestones through v1.3, and the approved development sequence from v1.4 through v2.0. The [Engineering Specification](ENGINEERING_SPEC.md) is authoritative for product behavior, architecture, compatibility, and scope. Milestone assignments do not authorize changes outside their stated scope.

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
- Track selected versus explicitly confirmed-for-writing disk definitions separately per image session. Require confirmation before the first modification under an unconfirmed definition; changing definitions invalidates confirmation. Do not infer confirmation from read success, `.FDF`, or remembered format. Coordinate with `.FDF` warnings to avoid duplicate dialogs without suppressing mandatory write or integrity warnings.
- Implement safe output publication and appropriate conflict, interruption, and concurrent-change handling.
- Implement default-on, preference-disableable single-version `.BAK` backups for destination image bytes and existing `.FDF` files actually replaced. Do not rotate image backup for sidecar-only Save. Abort on required backup failure unless the user explicitly retries without backup. Specify and test non-atomic image/sidecar publication, interrupted saves, recovery, and truthful partial-failure reporting; never silently restore backups.
- Design source identity and single-writer ownership using native file identity and locking, including symlink/hard-link aliases, source replacement during publication, crash recovery, and external-change revalidation; avoid indefinite stale lockouts.
- Implement file import/export, rename, confirmed deferred deletion, supported RO/SYS/ARC Properties editing, optional text conversion, timestamp handling where supported, and filename collision safeguards.
- Import valid CP/M filenames automatically; review names requiring 8.3 shortening/translation in one editable batch dialog, detecting within-batch and destination collisions. Use Replace/Skip/Cancel with Skip preselected and Apply to All for batch conflicts.
- Implement single-file F2/context-menu Rename as a metadata-only update across extents; reject rename collisions without Replace. Deletion requires confirmation, changes only the working image, and is reversible through Discard of all unsaved changes (not per-operation Undo).
- Implement same-filesystem cross-user-area moves by updating user bytes across extents, without copying file data; preserve metadata and enforce destination collision checks.
- Support independent export, import, and replacement of reserved system-track bytes on existing images. Use the selected disk definition to locate the region, protect filesystem data, validate size, and define short-input padding. The user, not CP/M Image, determines whether supplied system software is bootable or appropriate.
- Generate or update an image-specific, standalone cpmtools-compatible `.FDF` on explicit Save or Save As by default, including an otherwise unmodified image; never create it merely on open, detection, or format selection. Provide a preference to disable automatic creation.
- Detect conflicting existing sidecars and define safe image-plus-sidecar publication/recovery behavior.
- Treat `.FDF` as a record of the user-selected interpretation, not proof of the underlying disk format or a filesystem conversion. Implement a Confirm Disk Definition warning on first sidecar creation and when replacing a sidecar with a different definition; allow persistent suppression of routine warnings, but never suppress detected integrity warnings. Cancel must abort Save without publication.

**Acceptance:** Write confirmation is enforced before modification and invalidated by format changes; backup rotation and sidecar-only saves preserve the correct previous versions; failed backup/publication does not silently discard recoverable data; Save As preserves its source and switches the active tab only on success. Edited images reopen correctly; unrelated contents and source images remain protected on failure; system-track bytes round-trip accurately; generated `.FDF` files can be parsed by independent unmodified cpmtools; first-save/changed-definition warnings, suppression preference, and cancellation behave as specified.

## v0.5 — Container interoperability

- Support opening, editing, and saving qualified IMD, DMK, and JV3 layouts alongside RAW. Save As can create container output from an existing working image.
- Use pinned LibDsk for IMD and JV3 sector I/O and a dedicated adapter for ordinary MFM DMK.
- Decode containers to the **RAW layout expected by unmodified cpmtools** using the selected disk definition, not merely numeric sector-ID order. Apply sector translation/skew exactly once.
- Implement reverse mapping for container encoding and qualify emulator-compatible output.
- Provide IMD comment viewing, editing, and preservation in Image Information.
- Report missing/ambiguous sectors and conversion limitations; never silently invent or discard essential data.
- Derive mappings for canonical single-sided layouts and reuse the verified mapping for Save. Require explicit profiles for multi-sided or noncanonical layouts and Save As to IMD, JV3, or DMK.
- Defer broader container-derived mapping. Keep standalone Amstrad FDF generation and batch text-export conversion as deferred file-management work.

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
- Complete preferences, progress/error presentation, and general workflow integration.
- Provide a dedicated read-only Image Information (image-level Properties) window showing relevant image details, filesystem capacity and usage, geometry, container mapping, the selected disk definition, and available diagnostics and compatibility information. The window has no editing controls. Existing editing operations remain available through their existing UI workflows.
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

## v1.1 — Read-only file viewer

Use V, Return, double-click, or View → View File to inspect one selected file in the current working image. Support ASCII and UTF-8 text with CP/M line endings and EOF padding, a labeled 1 MiB limit, and text selection and copy. Use T for Text/Hex and 7 for optional high-bit filtering in Text mode. Hex displays original bytes and adapts to the window width. Recognize and decode standalone SQ/CR and browse LBR/ARK, including nested members, in independent windows. Use Q and Z in the main file list to force SQ and CR. Qualify supported variants and surface raw fallback for unsupported data. File editing remains deferred.

## v1.2 — Text export and archive-member extraction

- Recognize Ctrl-Z in the final CP/M record of valid text even when leftover bytes follow it. Preserve the complete stream in Hex.
- Allow explicit per-file text conversion when exporting selections, one user area, or All user areas. Preserve stored bytes by default.
- Export one viewed LBR or ARK member at any nesting level using the selected original or decompressed byte stream and the viewer's existing limits.
- Export selected members from the current LBR or ARK to one folder, with a byte-stream choice and no overwrite of existing files.

**Acceptance:** The SIG/M Pascal examples open as text, a binary fixture opens in Hex, mixed batches preserve unchecked file bytes, and exported archive members match their selected streams. Exports leave source images unchanged.

## v1.3 — Disk Format Editor

**Shipped November 16, 2026.** Create, duplicate, import, validate, save, and export standalone cpmtools disk definitions from the editor. Saved definitions appear in the format picker, and users can check a definition against an image and preview legacy mapping sidecar conversion before replacement.

See the [published v1.3 release and downloads](https://github.com/CPMArchives/cpmimage/releases/tag/v1.3#assets), [Disk Format Editor help](HELP.md#edit-disk-format-definitions), and [package validation results](VALIDATION.md#v13-disk-format-editor-package-checks).

## v1.4 — TD0 Support & Physical Disk Inspection

- Complete and qualify TD0 import, already available in v1.4 Beta 1. Import supported TD0 images into unsaved RAW tabs using the source basename and preserve the original archive.
- Retain useful source metadata already obtained during decoding and inexpensive to keep when opening or converting TD0, DMK, IMD, JV3, and other supported containers. Retention belongs to the image session and applies regardless of container type. The read-only metadata describes the original source, remains available to the Disk Format Editor and other investigation tools for the life of the tab, and is discarded on close. Keep it out of RAW images, `.FDF` files, and additional sidecars.
- Provide a read-only Physical Disk Inspector for observed tracks, heads, sector IDs, sizes, order, and available status and source metadata. Report unavailable information explicitly and distinguish source observations from the current working image.

## v1.5 — Format Investigation & Diagnostics

- Extend the existing Disk Format Editor to compare observed physical layouts with candidate disk definitions and show geometry, sector-mapping, and filesystem plausibility diagnostics.
- Improve existing assisted format detection and explain the evidence for candidate rankings, mismatches, and unresolved ambiguities.

## v1.6 — Filesystem Structure Inspection

- Provide a read-only Filesystem Structure Inspector for CP/M directory entries, extents, and allocation blocks under the selected disk definition. Show how entries and extents reference blocks.

## v1.7 — Image Comparison

- Compare images at the filesystem level across directory structures and CP/M user areas. Identify added, removed, changed, and identical files using file contents and attributes, with structural differences visible where relevant.

## v1.8 — Filesystem Checking & Controlled Repair

- Check filesystem consistency and report findings with the affected structures and supporting evidence.
- Determine the extent of repair in an engineering review. Approve only operations with understood effects and qualified safety behavior. Show the proposed changes and require explicit approval before applying them to the private working image. Use the existing Save and Discard workflow.

## v2.0 — Automated Disk Analysis & Recovery Assistance

- Extend existing format identification with deeper validation against available container metadata, geometry, sector mapping, directory entries, extent consistency, and allocation evidence.
- When the catalogue yields no plausible match, search a bounded set of CP/M 2.2 allocation, directory, reservation, and skew parameters. Draft definitions require a populated directory and a clean consistency check. Review them in the Disk Format Editor before saving or selecting one. Initially, search uses the qualified standard DSK layout, supported single-sided TD0 inventory, or the selected geometry of a working image. An external RAW file's length alone does not establish its track geometry.
- Automate filesystem analysis and comparison of related images. Explain inconsistencies, distinguish likely format mismatches from filesystem damage where evidence permits, and identify differences, duplicate contents, and potentially more complete or reliable copies.
- Produce evidence-based repair plans for review and require explicit user approval before applying any proposed modifications.
- Integrate navigation between physical observations, format definitions, filesystem structures, comparison results, and diagnostic findings within the relevant image session.

### Workbench interface (v2.0)

- Keep the Workbench menu available in the main application.
- Offer a separate **View → Workbench Toolbar** preference, off by default. Toggling the toolbar leaves open tools and drafts intact.

## v2.1 — Recovery from Image Comparisons and ImageDisk Integration

Development remains paused at v2.0. This milestone has no scheduled implementation start or release date.

### Read-only recovery from image comparisons

- From Compare Images, show each copy’s image name, CP/M user area, size, attributes, and whether its full contents could be read. Open either readable copy in the existing Text/Hex viewer. Present byte comparisons, readability, and filesystem findings separately so users can decide which copy to recover.
- Export one chosen copy from the current working image through the existing export dialog, filename translations, and conflict handling. Preserve the selected bytes by default. Report incomplete reads and leave no partial output.
- Validate with a known-good image and a damaged copy, plus a pair with two readable but differing versions. Compare exported bytes with independent cpmtools extractions where practical. Verify cancellation, filename conflicts, and preservation of both source images and their unsaved edits. Run focused macOS tests and Windows/Linux viewer and export smoke tests.

Batch recovery, transfers back into an image, automatic selection of a preferred copy, and new repair operations remain deferred. Broader container-derived mappings and standalone Amstrad FDF generation remain separate work.

### ImageDisk utilities and selective integration

1. Bundle Howard Harte’s six ImageDisk utilities and expose them through the existing headless dispatcher as `cpmwb /imda`, `/imdchk`, `/imdu`, `/imdcmp`, `/bin2imd`, and `/imdv`. Pin the upstream revisions, include license notices, and verify argument, stream, and exit-status passthrough on all five release targets, including interactive IMDV. Document that CLI editing operates directly on the supplied files. GUI integration follows this step.
2. Integrate qualified capabilities where they fill a Workbench gap. Start with an IMDA-style Physical Disk Inspector summary of required cylinders and heads, recorded modes and data rates, and an explicitly labeled maximum-track estimate. Evaluate physical IMD comparison as a complement to filesystem comparison. Keep LibDsk responsible for ordinary IMD I/O and retain existing validation and Save/Discard safeguards.

## Scope deferred from v1.0

- Graphical `fsck.cpm`-style consistency checking and controlled repair are scheduled for v1.8.
- Graphical `fsed.cpm`-style filesystem structure/sector editing.
- Low-level track/sector and flux editing.
- Filesystem-level comparison is scheduled for v1.7, with automated comparison and candidate disk-definition reconstruction in v2.0. Image merging remains deferred.
- Physical floppy-drive access.

Editable Image Properties remains exploratory work. Future design shall distinguish reinterpreting existing image bytes, changing image contents or metadata through the working-image and Save workflow, and authoring new reusable disk definitions.

The existing cpmtools CLI executables, including `fsck.cpm` and `fsed.cpm`, remain accessible in v1.0 headless mode.

## Engineering controls

- Maintain the Qt GUI → Python broker → isolated native helper architecture unless a consequential change is reviewed and approved.
- Use cpmtools as the authoritative filesystem engine and `diskdefs` interpreter; do not add an application-level format whitelist.
- Treat RAW compatibility with **independent, unmodified cpmtools** as the container interoperability invariant.
- Keep source images safe through the existing private working-image and Save/Discard model. Require explicit user approval for proposed diagnostic or repair modifications and explicit Save or Save As for publication.
- Preserve LibDsk's authority for supported container sector I/O and the existing dedicated adapters where applicable.
- Keep format investigation and filesystem maintenance distinct. They may share diagnostics and image-session context.
- Make analysis deterministic and explainable. Identify supporting evidence and distinguish plausible interpretations from verified findings.
- Make each intermediate release independently useful.
- Test and review each milestone independently. Do not interpret this roadmap as authorization to implement all milestones in a single Work assignment.
- Resolve open implementation details in the milestone where they matter, without reopening approved product requirements.
- Treat the approved 1.0 product/UI design as feature-frozen; **Show All User Areas is required** for 1.0, while the future file viewer is not a release blocker.
