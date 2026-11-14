# CP/M Image — Engineering Specification

**Version:** 1.0  
**Status:** Approved engineering baseline  
**Project:** CPMArchives/cpmimage  
**Target release:** 1.0  
**Date:** November 14, 2026

---

## 1. Purpose and Scope

### 1.1 Product definition

CP/M Image is an open-source, cross-platform graphical application for managing CP/M disk images.

Its intended role is analogous to WinZip for ZIP archives: users open disk images, browse their contents, manage files, and save modified images through a familiar desktop interface.

### 1.2 Primary objectives

CP/M Image shall:

1. Provide comprehensive CP/M disk-image file management.
2. Support all valid disk definitions accepted by its bundled cpmtools-derived filesystem engine.
3. Support common and historically relevant disk-image containers.
4. Generate standard RAW images independently usable by cpmtools.
5. Preserve original disk images during editing until changes are explicitly saved.
6. Provide consistent functionality across macOS, Windows, and Linux.
7. Reuse established filesystem and disk-image libraries rather than unnecessarily reimplementing them.

### 1.3 Version 1.0 scope

Version 1.0 shall provide:

- Opening and browsing existing CP/M disk images.
- Importing, exporting, renaming, deleting, copying, and moving files.
- Viewing and modifying supported file attributes.
- Viewing available file timestamps and preserving them where supported.
- Optional text-file conversion.
- Navigating CP/M user areas.
- Importing, exporting, and replacing system-track contents.
- Creating and formatting disk images.
- Saving modified images and saving copies under different names.
- Multiple simultaneously open images.
- Show All User Areas with flat multi-area export and duplicate-content handling.
- Narrow CP/M user-area navigation and a spacious file-list pane.
- Bidirectional drag-and-drop with native host file managers.
- Bundled searchable offline Help and companion online documentation.
- Drag-and-drop and clipboard operations.
- Full `diskdefs` catalogue support and dynamic format selection.
- Custom disk-format catalogues, favorites, and recent-image history.
- Portable image-specific `.FDF` disk definitions.
- Command-line image opening and headless access to the bundled cpmtools suite.
- RAW, IMD, DMK, and JV3 container interoperability.
- IMD image-comment viewing and editing.
- Filesystem capacity and usage information.

### 1.4 Post-1.0 scope

The following are explicitly excluded from 1.0:

- `fsck.cpm`-style filesystem consistency checking and repair.
- `fsed.cpm`-style filesystem structure inspection and editing.
- Low-level track/sector editing.
- Flux-level disk preservation and editing.
- Advanced disk-image analysis, comparison, and merging.
- Physical floppy-drive access.

These features may be considered after 1.0 but shall not delay the initial release.

---

## 2. Supported Platforms and Distribution

### 2.1 Operating systems

The application shall target macOS, Windows, and Linux.

### 2.2 CPU architectures

CP/M Image 1.0 shall provide native x86-64 builds for Windows and Linux, and native ARM64 builds for macOS.

Additional supported targets shall include macOS x86-64, Linux ARM64, and Windows ARM64, subject to successful build, packaging, and qualification.

The existing ARM64 validation on macOS, Windows, and Linux provides the initial development baseline but does not substitute for validation on the other target architectures.

Minimum operating-system versions shall be established during release qualification.

### 2.3 Self-contained distribution

Users shall not be required to install cpmtools, LibDsk, Python, or other command-line dependencies separately.

Distributed packages shall include necessary runtime components and applicable licensing and corresponding-source materials.

---

## 3. Application Architecture

### 3.1 Existing implementation

The development baseline consists of:

| Component | Technology | Responsibility |
|---|---|---|
| GUI | Qt 6 Widgets, C++17 | User interaction and presentation |
| Broker | Python | Sessions, requests, process supervision, working images |
| Native helper | C, cpmtools-derived | CP/M filesystem operations |
| Disk backend | cpmtools 2.23, LibDsk 1.4.2 | Filesystem interpretation and disk access |
| Format definitions | cpmtools `diskdefs` | CP/M disk-layout parameters |

The current prototype is read-only and restricts operational format selection to five hardcoded layouts.

### 3.2 Architectural commitments

The existing architecture shall remain the development baseline.

The application shall:

- Keep filesystem operations outside the GUI process.
- Retain the broker/helper process-isolation model.
- Use cpmtools as the authoritative filesystem implementation.
- Keep upstream modifications localized and documented.
- Avoid duplicating filesystem interpretation in GUI or broker code.
- Separate container decoding and encoding from CP/M filesystem operations.
- Preserve existing validated behavior during development.

Architectural replacement requires demonstrated technical justification.

### 3.3 Component interfaces

The GUI shall communicate with the broker through a versioned, validated request interface.

The broker shall manage image sessions and coordinate helper operations.

The native helper shall resolve disk definitions and perform filesystem operations without requiring the GUI to interpret CP/M directory or allocation structures.

Operations shall return structured results and errors.

Protocol changes shall be specified and tested during their respective milestones.

### 3.4 Dependency management

External dependencies shall remain version-pinned and reproducibly buildable.

Upgrades to cpmtools, LibDsk, Qt, or other runtime components require compatibility review and regression testing.

The existing odd-track geometry correction must remain effective until replaced by an independently validated solution.

---

## 4. Disk Format Definitions

**Implementation priority: v0.3**

### 4.1 Full diskdefs support

CP/M Image shall expose and support all valid disk definitions accepted by its bundled cpmtools-derived engine, without an application-maintained whitelist of individual formats.

The existing five-format restriction shall be eliminated.

Disk definitions shall be usable without adding format-specific code to the GUI, broker, or validation layer.

Original `diskdefs` identifiers shall remain valid.

Unsupported backend capabilities, incompatible image structures, or unsupported containers shall produce explicit errors rather than silently restricting the catalogue.

### 4.2 Catalogue sources

The application shall support:

1. A complete bundled `diskdefs` catalogue.
2. User-selected external catalogues.
3. User-defined additions and overrides.
4. Deliberately reduced catalogues containing only selected definitions.

The bundled catalogue shall be versioned with the application.

Catalogue loading shall not require an internet connection.

### 4.3 Catalogue interpretation

The cpmtools-derived parser shall remain authoritative for filesystem parameters.

The application may construct a normalized, indexed representation of successfully parsed definitions for runtime selection and presentation.

This representation shall preserve all functionally significant parameters, including applicable LibDsk references.

Original catalogue files shall remain intact.

The application shall not maintain a second, independently implemented interpretation of CP/M filesystem rules.

### 4.4 Format selection

The GUI shall provide:

- Searchable format names and identifiers.
- Manufacturer and model grouping.
- System-specific groupings, including BetterCP/M, where a manufacturer classification is inappropriate.
- Alphabetical organization.
- Generic or unclassified formats.
- Persistent favorites.

Where multiple display entries refer to the same underlying format, unnecessary duplication of operational definitions should be avoided.

Manufacturer classification shall not alter filesystem interpretation.

### 4.5 Favorites and catalogue changes

Favorites shall persist across application sessions.

When a selected catalogue lacks a previously favorited definition, the preference shall be retained but the unavailable format shall not appear as an active selection.

Unavailable favorites shall remain reviewable through settings.

### 4.6 Format switching

Users shall be able to change the selected disk definition for an open image without closing and reopening its source.

The application shall reinterpret the image and refresh the directory listing.

Selecting an incorrect format may produce invalid or meaningless results, but shall not compromise memory safety or modify the source.

A failed interpretation shall not automatically close the source image.

### 4.7 Format switching with unsaved changes

When a user selects a different disk format while the working image contains unsaved modifications, CP/M Image shall warn the user and offer:

- **Save:** Save modifications under the current format, then reinterpret the image using the newly selected format.
- **Discard:** Discard unsaved modifications, then reinterpret the original image using the newly selected format.
- **Cancel:** Retain the current format, working image, and unsaved modifications.

If saving fails or is cancelled, the format switch shall not proceed.

Changing formats when no unsaved modifications exist shall not require confirmation.

The source image shall remain open throughout the operation.

### 4.8 Recent format selections

The application shall remember the selected disk format for recently opened images.

Recent-image records shall retain sufficient source identity information to avoid blindly applying stale settings.

### 4.9 Image-specific FDF sidecars

When opening an image, CP/M Image shall search beside it for a matching `.FDF` file using the same filename stem, for example `mmcpm223.dmk` and `mmcpm223.fdf`.

A generated sidecar shall contain a complete, standalone cpmtools-compatible disk definition, including its identifier and any inherited parameters needed to interpret the image. It shall be independently usable by unmodified cpmtools without CP/M Image-specific syntax.

An explicit Save or Save As shall by default create or update the image's sidecar using the selected disk definition, even if the image has no content changes. Merely opening, detecting, or selecting a format shall not create a sidecar. The sidecar shall be generated only as part of an explicit image-save workflow. Users may disable automatic sidecar generation in preferences. An existing sidecar that conflicts with the selected definition shall not be silently overwritten. Saving the image and sidecar shall follow a documented failure-recovery policy that avoids silently leaving inconsistent results.

An `.FDF` records the **disk definition selected for interpretation**, not a verified identification of the image's actual filesystem format. Selecting a different definition, saving, or generating an `.FDF` shall not be described as converting the image to that disk format. A selected definition may be wrong even when the directory appears plausible. CP/M Image shall perform available structural and write-safety checks and report detected inconsistencies, but shall not claim to prove historical format identity. The user is responsible for selecting an appropriate definition.

When an explicit Save or Save As would create an `.FDF` for the first time, the application shall present a **Confirm Disk Definition** warning before publication, identifying the selected definition and explaining that the sidecar records the selection without verifying or converting the actual filesystem. The same warning shall appear when saving would replace an existing `.FDF` with a **different definition**. It shall not repeat on later saves when the definition is unchanged. The warning shall offer Continue Save and Cancel, and a persistent **Don't show this warning again** preference for these routine warnings. Cancel shall abort the entire Save operation without modifying the destination. Actual detected filesystem-integrity warnings shall never be suppressed by this preference.

Format-selection precedence is: (1) explicit `/f=FORMAT`, (2) matching `.FDF` sidecar, (3) remembered format, (4) normal selection or detection. Explicitly opening an image with a different format shall not itself rewrite the sidecar. Invalid sidecars shall be reported.

Sidecar discovery and loading belong to v0.3; sidecar generation and updating belong to v0.4.

### 4.10 Automatic format detection

Assisted detection shall use available container metadata, geometry, and filesystem plausibility to rank plausible disk definitions. The selection dialog shall show all candidates in ranked order, preselect the highest-ranked candidate, give tied candidates the same evidence description, and break ties deterministically. Users shall retain access to the full catalogue. Numerical confidence percentages require a defensible statistical basis.

A matching geometry or plausible directory is evidence for an interpretation of the image. Selecting a definition never converts the source filesystem. An explicit user selection shall take precedence over detection. For unfamiliar images, users shall confirm the selected format before writable operations.

v0.3 implements the candidate picker and read-only checks supported by the current backend. Broader detection heuristics and evidence thresholds are targeted for v0.8.

---

## 5. Disk-Image Containers and RAW Interoperability

### 5.1 Disk formats versus containers

A disk format describes the organization of a disk and its CP/M filesystem.

A container describes how the disk's contents are represented in an image file.

CP/M Image shall keep these concepts separate.

The application shall not assume a one-to-one relationship between a computer model, filesystem format, and image container.

### 5.2 RAW working representation

**RAW shall be the common working representation for CP/M filesystem operations.**

The intended processing sequence is:

1. Open the source image.
2. Decode its container into a RAW working image when necessary.
3. Interpret the RAW image using the selected disk definition.
4. Perform filesystem operations on the working image.
5. Encode the working image into the requested output container.
6. Validate and publish the completed output.

Compatible RAW images may be used directly as the basis for a private working copy.

### 5.3 Independent cpmtools compatibility

**Every RAW image generated by CP/M Image shall be independently recognizable and accessible by an unmodified cpmtools installation when supplied with the corresponding disk definition.**

The RAW image shall conform to cpmtools' expected:

- Disk geometry.
- Sector ordering.
- Sector translation and skew conventions.
- Filesystem organization.
- Reserved system-track layout.
- Directory and allocation structures.

No CP/M Image-specific metadata, patches, or additional conversion procedures shall be required to interpret the RAW image.

Where the selected definition is custom, that definition may be supplied separately to cpmtools.

The application shall not invent a private RAW format convention.

### 5.4 Sector ordering and translation

Container adapters shall reconstruct RAW images in the byte ordering expected by cpmtools for the selected disk definition.

Physical sector order, numeric sector-ID order, and CP/M logical sector order shall be distinguished.

Numeric sector-ID ordering shall not override a known disk-format-specific ordering.

The implementation shall account for translation and skew performed by cpmtools, ensuring that transformations are not applied twice.

Where sector mapping cannot be determined reliably, the application shall report the ambiguity rather than silently choose an ordering.

The reverse mapping shall be used when encoding RAW data into the destination container.

### 5.5 Container adapters

Container-specific decoding and encoding shall be encapsulated behind a common application-level interface.

Each adapter shall handle its own container structure and limitations.

The common interface shall not require all containers to expose identical internal metadata.

Adding a bespoke container should not require redesigning the filesystem engine.

### 5.6 Required 1.0 containers

| Container | Role |
|---|---|
| RAW | Primary working representation and general-purpose image |
| IMD | Historical disk-image interchange |
| DMK | TRS-80-oriented disk-image interoperability |
| JV3 | TRS-80-oriented disk-image interoperability |

All four are required 1.0 targets for reading, creating, modifying, and saving within their documented capabilities.

Existing limited CPCEMU DSK functionality may be retained. Broader CPCEMU DSK support is not currently a 1.0 requirement.

### 5.7 LibDsk integration

LibDsk shall be the preferred implementation candidate for decoding and encoding containers it already supports.

Its existing driver capabilities shall be evaluated before developing custom adapters.

The presence of a driver in LibDsk does not automatically establish that the current CP/M Image helper exposes or qualifies that capability.

DMK support shall be investigated separately because it is not established as a supported driver in the pinned LibDsk 1.4.2 version.

### 5.8 Bespoke container policy

DMK, JV3, and comparable historically specialized containers shall be treated as input/output representations.

CP/M Image is not required to implement their complete archival or low-level editing capabilities.

The objective is to:

- Extract a usable RAW image.
- Perform CP/M filesystem operations.
- Reconstruct a usable image in the target container.
- Maintain functional compatibility with the relevant computer or emulator.

Byte-for-byte reproduction of the original container is not required.

### 5.9 Conversion limitations

Some containers contain physical or track-level information that RAW cannot represent.

Such information may be lost during conversion.

The application shall not silently discard information essential to reliable filesystem interpretation or reconstruction of a usable disk.

Conversions that cannot be performed reliably shall fail with an informative error.

The original source shall remain unchanged.

### 5.10 Missing or damaged sectors

When a container contains missing, unreadable, or ambiguous sector data, CP/M Image shall not silently substitute invented contents.

The application may permit explicitly acknowledged substitutions for safe read-only inspection.

Substituted sectors and the fill value shall be reported.

Saving an image that depends on substituted data shall require an explicit, supported policy and shall not silently present the result as an undamaged disk.

### 5.11 IMD image comments

CP/M Image shall support viewing and editing IMD image comments.

Comments shall be accessible through Image Information.

Users shall be able to modify or replace comments.

Changes shall remain in the working session until saved.

Comment editing shall not modify filesystem contents.

Existing comments shall be preserved unless explicitly changed.

Invalid or unsupported comment data shall not be silently discarded.

### 5.12 Compression

Where supported by the selected container backend, compressed disk images should be handled transparently.

Reading and writing capabilities shall be distinguished.

A read-only compression driver shall not be advertised as supporting modification or saving in the same representation.

Additional compression formats are not automatically required merely because LibDsk supports them.

### 5.13 Container compatibility

Container support shall be tested independently of filesystem-format support.

A disk definition being available does not imply that every container associated with its computer is supported.

---

## 6. Filesystem Operations

### 6.1 Browsing

Users shall be able to:

- View files organized by CP/M user area.
- Navigate applicable user areas.
- Sort directory listings.
- Select individual or multiple files.
- Inspect filenames, sizes, attributes, and available timestamps.
- Refresh the current listing.

The existing 0–15 user-area interface is the initial baseline. The application shall not assume that every supported CP/M variant is necessarily restricted to those user numbers.

### 6.2 Export

The application shall support:

- Exporting individual files.
- Exporting multiple selected files.
- Exporting complete user areas.
- Configurable host filename translation.
- Destination conflict handling.
- Progress reporting and cancellation.
- Optional text-file conversion.
- Preservation of available file timestamps where supported.

Binary-preserving export shall be the default.

Text conversion shall be explicitly selected by the user.

The application shall document CP/M record-padding behavior.

### 6.3 Import

Users shall be able to import host files into a selected CP/M user area.

Import shall validate filename compatibility, directory capacity, filesystem space, and destination conflicts.

Optional text conversion shall be supported.

Binary-preserving import shall be the default.

Import shall not damage unrelated files when an operation fails.

Host-to-CP/M timestamp preservation shall be supported only where the filesystem engine and target format provide the necessary functionality.

### 6.4 Rename and delete

Users shall be able to rename and delete files in the working image.

Rename shall preserve file contents and applicable attributes.

Deletion shall not modify the original source until the resulting image is saved. Delete shall always require confirmation, with one prompt for a multiselection. It modifies only the private working image, marks the image dirty, and is committed on Save. Discard restores the entire last-saved state, not merely the most recent deletion. General undo/redo is not required for 1.0.

### 6.5 Copy and move

The application shall support copying and moving files:

- Within an image.
- Between user areas.
- Between simultaneously open images.

A move shall not discard source data before the destination operation has succeeded.

Moving between user areas on the same filesystem shall be a metadata-only operation that updates the user byte in every extent of the file, without copying allocation blocks, subject to destination-name collision checks and safe working-image updates. Copying between user areas allocates separate data and directory entries; moving between distinct images copies successfully before removing the source.

Destination conflicts, insufficient space, and incompatible filenames shall be handled predictably.

### 6.6 File attributes

**Implementation priority: v0.4**

CP/M Image shall allow users to view and modify supported CP/M file attributes.

The application shall support standard attributes, including:

- Read-only.
- System.
- Archive.

Other supported user-defined attribute bits may be exposed in technical details where reliably supported; the standard Properties editor shall provide RO, SYS, and ARC checkboxes.

Attribute editing shall be available through file properties and appropriate context-menu commands.

Changes shall be applied to the private working image and follow the same Save / Discard rules as rename and delete.

Attribute modifications shall preserve file contents and unrelated directory metadata.

### 6.7 File timestamps

Where supported by the selected filesystem variant, CP/M Image shall display available creation and modification timestamps.

Export shall preserve timestamps where supported by the filesystem engine.

Unsupported timestamp fields shall not be fabricated.

Timestamp handling shall account for the conventions of the selected CP/M filesystem variant.

### 6.8 Filename handling

The application shall distinguish original CP/M filename identities from host filenames.

The existing configurable export mappings are:

- `/` to `_`
- `:` to `~`
- `\` to `^`

These mappings shall remain configurable and consistent across host platforms.

Filename collisions shall be detected.

Import shall not assume that reversing a filename substitution necessarily reconstructs the original CP/M filename.

### 6.9 Import filename review and conflicts

Host filenames already valid under the selected CP/M format shall import automatically, with lowercase converted to uppercase where appropriate. For names requiring shortening or character translation, the application shall propose valid 8.3 names and present one editable review dialog for the batch before import; no silent truncation is permitted. Validate all proposed names and detect both destination and within-batch collisions.

File-transfer conflicts shall offer Replace, Skip, and Cancel, with **Skip preselected**. Apply to All shall be available for batch conflicts. Skip preserves the destination and continues; Cancel stops remaining operations; Replace follows safe-write rules. Report skipped files. Export-name collisions caused by host filename translation follow the same protection policy.

Rename is separate from transfer conflict handling: F2 or the context menu shall edit one filename at a time, validate CP/M naming rules, uppercase applicable input, and reject same-user-area collisions without offering Replace. Renaming shall preserve data blocks, attributes, and supported timestamps, updating all relevant extents in the working image until Save.

### 6.10 File size semantics

The file list shall show **Records** (CP/M 128-byte logical records) and **Size (bytes)** (records × 128), both sortable numerically. Byte counts shall be displayed as full integers with thousands separators and shall not be represented as exact original content lengths where the filesystem does not store that information. Allocated space may be shown in Properties technical details. Supported filesystem-specific exact-length metadata may be used only when genuinely available.

### 6.11 Filesystem statistics

The application shall display ordinary filesystem information, including where available:

- Total disk capacity.
- Used and free space.
- Allocation block size.
- Directory capacity.
- Used and available directory entries.

This is informational file-management functionality, not filesystem consistency checking.

### 6.12 CP/M Plus metadata

Where supported by the filesystem engine, CP/M Image shall recognize and display relevant CP/M Plus metadata, including disk labels and timestamps.

CP/M Plus password management is not a committed 1.0 feature.

Recognizing password metadata shall not be represented as enforcing password protection.

---

## 7. System-Track Management

**Implementation priority: v0.4**

### 7.1 Independent operation

CP/M Image shall allow users to import, export, and replace reserved system-track contents independently of image creation or formatting.

System-track management shall be available for existing images.

### 7.2 System-track identification

The selected disk definition shall determine the reserved system-track region.

The operation shall be unavailable when the selected format has no reserved system tracks.

### 7.3 Import and replacement

Users shall be able to select a binary file containing system-track data.

The application shall:

- Validate input size against system-track capacity.
- Write the supplied data to the correct sectors.
- Preserve directory and data areas.
- Apply changes to the private working image.
- Follow normal Save / Discard rules.

If supplied data is shorter than the reserved area, the application shall use an explicit, documented padding policy rather than silently choosing one.

### 7.4 Export

Users shall be able to export reserved system-track contents as a binary file.

The exported data shall correspond to the selected disk definition's reserved system-track region.

### 7.5 Responsibility boundary

CP/M Image is responsible for correct placement and preservation of supplied binary data.

The user is responsible for determining whether the supplied system software is appropriate for the target computer, disk format, BIOS, bootloader, and hardware.

CP/M Image shall not attempt to validate or certify operating-system compatibility or guarantee that the resulting disk will boot.

### 7.6 Relationship to formatting

The formatting workflow shall reuse the system-track import functionality.

System-track replacement shall not require reformatting the filesystem.

---

## 8. Image Lifecycle

### 8.1 Opening

Opening an image shall establish a working session containing:

- Source-image identity.
- Selected disk definition.
- Container type.
- Working-image state.
- Modification status.

### 8.2 Working-image modifications

Filesystem changes shall be applied to a private working image.

The application shall distinguish clean and modified sessions.

Users shall be able to discard unsaved modifications.

### 8.3 Save

Save shall produce a validated output before publishing it to the intended destination.

The implementation shall protect against incomplete operations and detect relevant changes to the source made by another process.

The exact save-publication mechanism shall be designed during the safe-write milestone. It shall coordinate publication with the ownership mechanism, including replacement of locked files and failure recovery.

### 8.4 Save As

Save As shall permit saving the working image under a different filename.

Where supported, users may select a different output container.

Conversion limitations shall be reported before output publication.

### 8.5 Unsaved changes

Closing or replacing a modified image shall offer Save, Discard, and Cancel choices.

Unsaved modifications shall not be silently discarded.

Format switching shall follow Section 4.7.

### 8.6 Image creation

Users shall be able to create new images using a selected disk definition and supported output container.

The resulting image shall contain a correctly initialized filesystem.

Generated RAW images shall satisfy the independent cpmtools compatibility requirement in Section 5.3.

### 8.7 Formatting

Creating and formatting a new image and formatting an existing image shall use the same underlying formatting engine.

Formatting an existing image shall require explicit confirmation.

Formatting shall support, where applicable:

- Filesystem initialization.
- Optional system-track binary data.
- CP/M Plus disk labels.
- Timestamp initialization.

The formatting workflow shall reuse independent system-track management functionality.

Formatting shall not modify the original source before the user explicitly saves the result.

---

## 9. Data Integrity and Safety

### 9.1 Source preservation

Ordinary editing operations shall not modify the original source image directly.

Failures during editing shall not damage the source.

### 9.2 Safe publication

Output shall be prepared and validated before replacing an existing destination.

The save mechanism shall address:

- Interrupted operations.
- Destination conflicts.
- Concurrent source changes.
- Publication failures.
- Recovery of unsaved work.

### 9.2.1 Concurrent ownership and external changes

The implementation shall distinguish source identity from a mere path string and enforce one writable owner per source across independent CP/M Image instances. Symlink/hard-link aliases, source replacement, lock acquisition failures, crashes, and stale ownership state shall be covered by targeted tests. Advisory locking shall not be treated as protection against noncooperating external programs; detect source changes before publication and fail safely when the original no longer matches the expected version.

### 9.3 Validation boundaries

Validation shall distinguish:

- Unsafe or out-of-bounds image access.
- Unsupported backend capabilities.
- Container decoding failures.
- Filesystem inconsistencies.
- Invalid user operations.

Memory and bounds safety shall never be relaxed.

Filesystem inconsistencies may be reported without automatically prohibiting safe read-only inspection.

### 9.4 Internal validation versus fsck

CP/M Image shall perform the validation necessary for safe filesystem operations.

This does not constitute implementing `fsck.cpm` functionality.

General-purpose filesystem consistency checking and repair are explicitly deferred until after 1.0.

### 9.5 Process isolation

The native helper shall remain isolated from the GUI.

Existing request validation, timeouts, cancellation, and process-containment protections shall be retained.

### 9.6 Error reporting

Errors shall be understandable and actionable.

Where technically possible, the application shall distinguish an incorrect disk definition, unsupported container, unsupported backend feature, and damaged image.

---

## 10. Desktop Interface

### 10.1 File-manager conventions

The application shall provide:

- Menus and context menus.
- Standard keyboard shortcuts.
- Multiple selection.
- Drag-and-drop.
- Clipboard operations.
- File properties.
- Progress reporting and cancellation.
- Image information.
- Unsaved-change indicators.

### 10.2 Primary navigation layout

The primary window shall use a narrow left sidebar for CP/M user areas and a larger right pane for the files in the selected user area. It shall not embed a permanent host filesystem browser.

The sidebar shall show numbered user-area folders (0–15, or 0–31 where supported), including empty areas, with a visual distinction between populated and empty folders. Its default width should be approximately 90–120 pixels, adjustable by a splitter. The right pane is the main workspace, with sortable filename, size, attribute, and available timestamp columns.

**Show All User Areas is required for 1.0.** The sidebar shall provide an **All** entry above the numbered areas. Selecting it displays a virtual combined listing of all user areas, without creating a CP/M directory. Host filesystem browsing shall use native file-open dialogs and the operating system's file manager.

### 10.3 Native file-manager drag-and-drop

CP/M Image shall interoperate with Finder, Windows Explorer, and supported Linux file managers. Dropping an image onto the appropriate application-level target shall open it; dropping host files onto a CP/M user area shall import them; dragging CP/M files to a host file manager shall export them; and dragging between open CP/M images shall transfer files.

Drop targets shall distinguish opening an image from importing the same file as ordinary CP/M data. Ambiguous actions shall be resolved explicitly. Existing filename translation, conflict handling, cancellation, and save-safety requirements apply.

Native drag-out exports shall keep temporary files available for asynchronous consumption by destination applications. Ending the drag gesture alone does not establish that the destination has finished reading. Cleanup shall follow native platform behavior and account for cancellation and failures without removing data still in use. Exports shall never modify source images. Use native facilities where practical and avoid a dedicated interprocess protocol. Implementation is targeted for v0.7 and native cross-platform qualification for v0.9.

### 10.4 Multiple images and independent instances

Each application window shall provide tabs for multiple images. Users shall also be able to launch multiple independent application processes, each with its own tabbed workspace. Every tab shall retain its selected format, selected user area, working state, and unsaved-change indicator.

Multiple processes may open the same source read-only, but only one writable session shall own a given source at a time; subsequent sessions shall not silently acquire write access. Source changes shall be revalidated before publication.

Writable ownership shall be enforced using appropriate host operating-system file identity and locking facilities, not pathname-string comparison alone. The implementation shall account for aliases such as symbolic links and hard links where supported, and shall not silently treat two names for the same underlying file as independent writable images. It shall also handle replacement of a source file during safe publication, since a lock on a previous file object may not protect its replacement. Locking is cooperative within CP/M Image; external editors may not honor it, so revalidation of source identity and contents immediately before Save remains mandatory.

When a second instance encounters a source already owned for writing, it shall offer or enter read-only access without silently taking ownership. After a successful Save As, the active session shall acquire writable ownership of the new destination and release ownership of the old source only after publication and the session transition succeed. If the new destination cannot be safely owned, Save As shall not silently leave two writers. Locks shall be released on normal close and recover after process crashes; persistent stale lock files shall not indefinitely prevent access. Closing a modified tab, window, or instance shall offer Save / Discard / Cancel.

Transfers between tabs in one instance may be coordinated internally; cross-instance transfers shall use native operating-system clipboard/drag-and-drop facilities without a dedicated interprocess transfer protocol.

### 10.4.1 Clipboard and transfer semantics

Copy/Paste duplicates files without modifying their sources. Cut/Paste moves files, removing the source only after successful destination writing. Drag-and-drop between user areas or images copies by default; moving requires explicit user action. Clipboard and drag-and-drop shall share the approved conflict policy and safe-write rules.

### 10.5 File-list columns and interaction

The default columns, in order, shall be **Filename**, **Records**, **Size (bytes)**, and **Attributes**. User area and supported timestamp columns are optional in an individual user-area view; **Show All shall automatically display the User column** so duplicate filenames from different areas remain distinguishable. Returning to an individual area restores its ordinary column configuration, including any explicitly enabled User column. Column headers sort ascending/descending, with filename ascending by default and numeric sorting for record and byte counts. Columns shall be resizable, reorderable, and configurable through a column-header Choose Columns context menu. Visibility, order, and widths shall persist.

Single-click selects a file; double-click opens Properties in 1.0. A future viewer may replace double-click behavior, with Properties remaining in the context menu. F2 renames and Delete invokes confirmed deletion. Multiselection shall follow native platform conventions.

Properties shall display filename, user area, logical size, available timestamps, and optional directory extent/allocation details. RO, SYS, and ARC are editable; renaming and moving user areas are separate operations. Unsupported timestamps shall not be fabricated.

### 10.5.1 Show All navigation and selection

View → Show All User Areas shall be a checked toggle, with **Ctrl/Cmd+Shift+A** as its keyboard shortcut. Enabling Show All displays every user area's files and their User numbers; disabling it returns to the previously selected user area, remembered independently per image tab. Switching views clears file selections to prevent actions on hidden selections.

All selected files shall retain their source user-area identities, including during sorting, multiselection, Properties, copying, moving, deletion, and export. Operations requiring a destination user area (including Import and Paste) shall prompt for one while Show All is active, rather than silently choosing User 0.

### 10.5.2 Flat multi-area export and deduplication

When files from multiple user areas are exported or dragged to a host folder from Show All, the default shall be a **flat export** into that destination folder, not automatic USERnn subdirectories. Group source files by their translated host destination names. Compare files with the same destination name by logical CP/M content, including final-record padding: compare lengths first, use checksums as an optimization, and verify byte equality before treating files as identical.

Identical copies shall be exported once and subsequent copies skipped with an explicit report. If the destination host folder already contains an identical file, skip the redundant export. Different-content collisions, whether among selected source files or against existing destination files, shall invoke export conflict resolution.

The export conflict actions shall be **Overwrite**, **Skip** (preselected), **Rename** (choose a distinct valid host filename), and **Cancel**. Apply to All shall be offered for Overwrite and Skip, not Rename or Cancel. Skip omits only the current file and continues; Cancel stops the current and all remaining files, without rolling back completed exports. Report skipped files and consolidated duplicates.

Flat export with deduplication is a convenience operation; it does not promise archival preservation of user-area organization or all CP/M metadata. The approved general import/copy collision policy remains unchanged outside this export-specific Rename option.

### 10.6 Toolbar and context menus

The toolbar shall contain exactly five labeled primary actions in two groups: **Open, New, Save** | **Import, Export**. Delete, sorting, Rename, Properties, Help, and format selection shall not require dedicated toolbar buttons. The toolbar shall be visible by default, hideable via View → Toolbar, with application-wide persistent visibility and full recovery of its screen space. Every toolbar command shall remain accessible from the menus.

Context menus shall be defined by target:

| Target | Commands |
|---|---|
| Selected file(s) | Export, Copy, Move to User, Rename, Delete, Properties |
| User-area folder | Open User Area, Import Files, Paste, Export All, Properties |
| Empty file-list area | Import Files, Paste, Select All, Refresh |
| Image tab | Save, Save As, Image Information, Close Image |
| File-list column header | Choose Columns |

Right-clicking a selected file shall preserve the current multiselection; right-clicking an unselected file shall select it. Commands inappropriate for the current selection/state shall be disabled or omitted (e.g., Rename for multiple files). Menus, shortcuts, and toolbar actions shall invoke the same underlying commands. Native secondary-click, Control-click where appropriate, and keyboard context-menu access shall be supported.

### 10.7 Keyboard shortcuts and function keys

CP/M Image shall provide the following function-key assignments, subject to native host-platform conventions:

| Key | Action |
|---|---|
| F1 | Open Help, context-sensitive where available |
| F2 | Rename the selected file |
| F3 | Reserved for a future file viewer; no 1.0 action |
| F4 | Open File Properties |
| F5 | Refresh the current file listing |
| F6 | Move the selected file(s) to another user area |
| F7–F9 | Unassigned |
| F10 | Native menu activation where supported |
| F11–F12 | Unassigned |

Standard application shortcuts shall be:

| Shortcut | Action |
|---|---|
| Ctrl/Cmd+O | Open disk image |
| Ctrl/Cmd+S | Save active image |
| Ctrl/Cmd+C | Copy |
| Ctrl/Cmd+X | Cut |
| Ctrl/Cmd+V | Paste |
| Ctrl/Cmd+A | Select all files in the active list |
| Ctrl/Cmd+Shift+A | Toggle Show All User Areas |
| Delete | Delete selected files, with confirmation |

Use Command on macOS and Control on Windows/Linux as appropriate. Do not override operating-system function-key or hardware-control behavior. Keyboard shortcuts, menus, context menus, and toolbar controls shall invoke the same underlying commands, with availability governed by current selection and image state. F3 is reserved rather than implemented as a viewer in 1.0.

### 10.8 Recent images

The application shall remember recently opened images and their selected formats.

Users shall be able to pin frequently used images.

Stored source information shall be validated when images are reopened.

### 10.9 Preferences

Persistent preferences shall include relevant format-catalogue settings, favorites, filename translations, and application behavior.

### 10.10 Image information

The Image Information interface shall present relevant details, including:

- Source-image location.
- Selected disk definition.
- Container type.
- Known physical geometry.
- Filesystem capacity and usage.
- Source identity or fingerprint where appropriate.
- Container-specific metadata, including IMD comments.

Technical information shall remain accessible without dominating ordinary file-management workflows.

---

## 11. Help and Documentation

### 11.1 Primary offline Help

CP/M Image 1.0 shall include complete, searchable Help bundled with the application, accessible through the Help menu and F1 where appropriate. Essential Help shall work without an internet connection.

### 11.2 Online documentation

The same documentation shall be published online and accessible through the Help menu in the default browser. The online site is secondary; offline Help is the primary experience.

### 11.3 Single source and context-sensitive topics

Help content shall be maintained as Markdown in the project repository and used to generate both offline and online documentation. A lightweight existing viewer/generator is preferred over a bespoke Help engine.

Relevant dialogs and workflows shall link to their topics where practical. Coverage shall include opening images, user areas, file operations, filename translation and CP/M record padding, diskdefs and .FDF sidecars, RAW/IMD/DMK/JV3, system tracks, image creation, save safety, command-line operation, and troubleshooting.

### 11.4 Delivery

Documentation shall be developed alongside features. Offline Help, search, and contextual entry points shall be integrated in v0.8. Online publication and documentation qualification shall be completed in v0.9.

---

## 12. Command-Line Interface

### 12.1 Mode precedence

CP/M Image has GUI and headless cpmtools operating modes. A recognized cpmtools utility selector anywhere on the command line shall take absolute precedence: **the GUI must not initialize or open**, regardless of any other arguments or their order. The entire argument list shall be scanned before mode selection.

Multiple utility selectors shall cause a command-line error. GUI-only arguments supplied with a utility selector shall cause a command-line error unless specifically supported in headless mode; neither case shall launch the GUI.

### 12.2 GUI invocation

The GUI shall support `/img=PATH` and unambiguous positional image paths, `/f=FORMAT` to select a persistent format for subsequent images, and `/diskdefs=PATH` to select a disk-definition catalogue globally for the invocation. Corresponding long options `--image`, `--format`, and `--diskdefs` shall be available for conventional host-shell usage.

A selected `/f` overrides a matching image sidecar; without `/f`, Section 4.9 governs format selection. `/diskdefs` selects the catalogue, not a particular format, and applies to the invocation as a whole. Conflicting repeated catalogue options or an invalid specified catalogue shall cause explicit errors. Host absolute paths beginning with `/` shall not be misinterpreted as options.

### 12.3 Headless cpmtools invocation

`cpmimage /UTILITY [utility arguments]` shall dispatch to the identically named utility from the pinned bundled cpmtools distribution, including `/cpmcp`, `/cpmls`, `/cpmchattr`, `/fsck.cpm`, and `/fsed.cpm`. The complete bundled suite shall be available. No aliases or renamed cpmtools commands are required.

The dispatcher shall preserve original utility argument semantics, standard input, standard output, standard error, and exit status. It shall not reimplement cpmtools command parsers or filesystem operations. The shared `/diskdefs=PATH` option shall configure the utility's catalogue through cpmtools-supported mechanisms without forwarding the CP/M Image-specific option as an invalid utility argument.

Standalone utilities shall be built from the pinned source and packaged for every supported platform, without requiring a separate cpmtools installation. Windows console behavior shall be tested explicitly; a GUI-subsystem executable shall not be assumed to provide correct console streams or exit codes.

### 12.4 Scope and milestone

Exposing existing `fsck.cpm` and `fsed.cpm` in headless mode does not introduce their GUI equivalents into 1.0. GUI checking, repair, and low-level editing remain post-1.0. GUI catalogue-option handling begins in v0.3; the full dispatcher and packaging integration are targeted for v0.8.

---

## 13. Verification and Qualification

### 13.1 General requirements

Each milestone shall have independently testable acceptance criteria.

Regression testing shall protect previously implemented functionality.

### 13.2 Disk-format compatibility

Testing shall establish that valid supported definitions are resolved through the cpmtools-derived interface rather than an application-level whitelist.

Tests shall include differing geometry, allocation parameters, and sector translation.

### 13.3 Independent RAW compatibility

Every supported RAW-generation workflow shall be tested for compatibility with an independent, unmodified cpmtools build.

Tests shall verify:

- Successful opening using the corresponding disk definition.
- Correct directory listings.
- Correct exported file contents.
- Correct sector ordering and translation.
- Absence of required CP/M Image-specific metadata.

Tests shall include images where physical sector order, numeric sector-ID order, and logical filesystem order differ.

For custom definitions, the same definition shall be supplied to the independent cpmtools installation.

### 13.4 Filesystem operations

Write operations shall be verified through save, reopen, and content comparison.

Tests shall include:

- Insufficient space.
- Directory exhaustion.
- Filename conflicts.
- Attribute preservation.
- Timestamp behavior where supported.
- Multi-extent files.
- Interrupted operations.
- Source-image preservation.

### 13.5 System tracks

Tests shall verify:

- Correct system-track region selection.
- Exact export of reserved data.
- Correct replacement and placement.
- Preservation of directory and data areas.
- Rejection of oversized inputs.
- Defined behavior for shorter inputs.
- Save and reopen correctness.

Tests shall not attempt to certify the suitability of arbitrary operating-system binaries.

### 13.6 Containers

Container adapters shall be tested using appropriate images and, where relevant, target emulators.

Successful conversion requires a usable resulting disk, not byte-identical container reproduction.

Tests shall cover:

- IMD, DMK, and JV3 decoding and encoding.
- Sector ordering and translation.
- Missing-sector and ambiguous-sector handling.
- Conversion limitations.
- IMD comment preservation and editing.
- Save and reopen behavior.
- Independent cpmtools compatibility of generated RAW images.

### 13.7 Cross-platform qualification

The project shall establish an explicit operating-system and architecture matrix before 1.0.

Existing ARM64 validation is the starting baseline.

### 13.8 Packaging

Distributed packages shall include required dependencies and applicable licensing materials.

Package relocation, runtime independence, and installation behavior shall be tested.

### 13.9 Sidecar and command-line qualification

Tests shall verify generated `.FDF` files using independent cpmtools, image/sidecar naming and precedence, conflicting sidecars, and interrupted multi-file saves. Tests shall also verify multi-image GUI invocation, persistent format selection, catalogue override, headless precedence regardless of argument order, conflicting utility selectors, console streams, and exit status on supported platforms.

---

### 13.10 Interface and Help qualification

Verify source alias detection, independent-instance single-writer enforcement, read-only secondary access, crash recovery, safe ownership transfer after Save As, and source revalidation across atomic replacement. Verify the narrow user-area sidebar, populated/empty indicators, Show All toggle and per-tab return state, automatic User column, multi-area selection identity, flat export deduplication, export conflict choices and batch cancellation, default and optional columns, numeric sorting, column persistence, hideable five-action toolbar, context-menu selection rules, file Properties, independent instances and writable ownership, and native appearance on each supported platform. Exercise image-opening drops, host-to-image imports, image-to-host exports, and cross-image transfers.

Verify bundled offline Help without network access, search, contextual links, and consistency with the published online documentation.

---

## 14. Development Priorities

The implementation sequence shall be:

1. General `diskdefs` backend support.
2. Bundled/custom catalogue integration, `.FDF` discovery, command-line catalogue selection, and dynamic format selection.
3. Safe filesystem writing and core file operations, including attributes, system-track management, and `.FDF` sidecar saving.
4. IMD, DMK, and JV3 container interoperability.
5. Image creation and formatting.
6. Multiple-image management and desktop integration.
7. GUI command-line opening, headless cpmtools dispatch, desktop-interface refinement, offline/online Help, usability improvements, compatibility testing, and release qualification.

The companion roadmap shall define milestone numbers, dependencies, deliverables, and completion criteria.

Full `diskdefs` support is foundational and shall not be postponed until the application approaches 1.0.

---

## 15. Open Engineering Decisions

The following decisions remain open and shall be resolved during their relevant milestones:

| Issue | Resolution milestone |
|---|---|
| General diskdefs resolution API | v0.3 |
| Catalogue metadata and classification implementation | v0.3 |
| Safe save/publication mechanism | v0.4 |
| Image/sidecar publication and recovery policy | v0.4 |
| System-track short-input padding policy | v0.4 |
| Container conversion implementation and fidelity limits | v0.5 |
| Missing-sector substitution and save policy | v0.5 |
| Platform-specific multiple-instance file identity, locking, and replacement mechanism | v0.4/v0.7 |
| Assisted format-detection ranking and evidence thresholds | v0.3/v0.8 |
| Supported operating-system and architecture matrix | Release qualification |

These decisions shall not block unrelated approved work.

---

## 16. Project Governance

### 16.1 Specification authority

Once approved, this document shall serve as the authoritative engineering specification for CP/M Image 1.0.

The companion roadmap shall govern milestone sequencing.

### 16.2 Architectural changes

Consequential architectural changes shall be reviewed against this specification.

Reviews shall identify compatibility impacts, alternatives, implementation costs, and justification.

### 16.3 Implementation status

Requirements, implementation status, and qualification evidence shall be tracked separately.

A requirement's inclusion in this specification does not imply it has been implemented.

### 16.4 Relationship to other projects

CP/M Image shall not depend on completion of the separate universal CP/M disk-format database project.

Future integration may be evaluated independently.

---

## Appendix A — cpmtools Capability Mapping

| cpmtools capability | CP/M Image disposition |
|---|---|
| `cpmls` directory listing | 1.0 |
| `cpmcp` import/export | 1.0 |
| `cpmcp -t` text conversion | 1.0 |
| `cpmcp -p` export timestamp preservation | 1.0 where supported |
| `cpmrm` file deletion | 1.0 |
| `cpmchmod` permissions | 1.0 |
| `cpmchattr` attributes | 1.0 |
| `mkfs.cpm` filesystem creation | 1.0 |
| `mkfs.cpm -b` system-track initialization | 1.0, also independently available |
| `mkfs.cpm -L` disk labels | 1.0 where supported |
| `mkfs.cpm -t` timestamp initialization | 1.0 where supported |
| `fsck.cpm` consistency checking and repair | Post-1.0 |
| `fsed.cpm` filesystem inspection and editing | Post-1.0 |

## Appendix B — LibDsk and ImageDisk Capability Mapping

| Capability | CP/M Image disposition |
|---|---|
| LibDsk RAW image handling | 1.0 |
| LibDsk IMD handling | 1.0 |
| LibDsk JV3 handling | 1.0 |
| DMK container handling | 1.0; separate adapter investigation |
| LibDsk image conversion | 1.0 where needed for container adapters |
| LibDsk geometry identification | 1.0 where useful for image information |
| IMDU-style IMD-to-RAW conversion | 1.0 |
| BIN2IMD-style RAW-to-IMD conversion | 1.0 |
| IMD comment viewing and editing | 1.0 |
| IMD sector ordering and mapping | 1.0 |
| Missing-sector reporting | 1.0 |
| Transparent supported compression | Candidate enhancement |
| Detailed disk-image comparison | Post-1.0 |
| Disk-image merging and reconstruction | Post-1.0 |
| Track/sector editing | Post-1.0 |
| Physical disk-drive operations | Outside 1.0 |
