# CP/M Image — Development Roadmap

**Status:** Approved development sequence, reconciled with Engineering Specification 0.5  
**Project:** CPMArchives/cpmimage  
**Target release:** 1.0  
**Date:** November 13, 2026

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
- Establish format-selection precedence: explicit `/f`, matching image `.FDF`, remembered format, then ordinary selection/detection.
- Preserve the Save / Discard / Cancel policy for format changes with unsaved work as writing support becomes available.

**Acceptance:** A user can choose any backend-supported definition, open and reinterpret images without closing them, and use custom/reduced catalogues through the GUI.

**Release policy:** v0.3.1–v0.3.3 are independently testable development checkpoints; they need not be published as three separate releases. Stop for maintainer review after the complete v0.3 milestone.

## v0.4 — Safe writing and file management

- Introduce private mutable working images, dirty-state tracking, Discard, Save, and Save As.
- Implement safe output publication and appropriate conflict, interruption, and concurrent-change handling.
- Implement file import/export, rename, delete, supported attribute editing, optional text conversion, timestamp handling where supported, and filename collision safeguards.
- Support independent export, import, and replacement of reserved system-track bytes on existing images. Use the selected disk definition to locate the region, protect filesystem data, validate size, and define short-input padding. The user, not CP/M Image, determines whether supplied system software is bootable or appropriate.
- Generate or update an image-specific, standalone cpmtools-compatible `.FDF` when saving, by default, with a preference to disable automatic creation.
- Detect conflicting existing sidecars and define safe image-plus-sidecar publication/recovery behavior.

**Acceptance:** Edited images reopen correctly; unrelated contents and source images remain protected on failure; system-track bytes round-trip accurately; generated `.FDF` files can be parsed by independent unmodified cpmtools.

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

- Support multiple open image sessions with independent formats and unsaved states.
- Support copying/moving files between images and user areas.
- Implement drag-and-drop, clipboard transfers, multiple selection, and conflict handling.
- Integrate recent images and pinned image shortcuts.

**Acceptance:** Cross-image transfers preserve contents, handle partial failures safely, and do not lose source data before successful destination writes.

## v0.8 — Integration, command line, and usability

- Support GUI command-line image opening with `/img=PATH`, persistent `/f=FORMAT` for subsequent images, and global `/diskdefs=PATH`; provide conventional long-option equivalents.
- Implement a headless dispatcher for the **complete bundled cpmtools utility suite**, using original utility names and syntax, including `/cpmcp`, `/cpmchattr`, `/cpmls`, `/fsck.cpm`, and `/fsed.cpm`.
- A recognized utility selector **anywhere** on the command line always prevents GUI initialization. Multiple selectors or incompatible GUI-only options cause explicit errors, never GUI launch.
- Build and package the pinned standalone cpmtools executables for each supported host platform. Preserve arguments, stdin/stdout/stderr, and exit codes, with explicit Windows console qualification.
- Complete preferences, Image Information, relevant format-detection assistance, progress/error presentation, and general workflow integration.

**Acceptance:** GUI image/format arguments behave predictably; headless commands match the corresponding bundled utilities; the GUI never opens for utility dispatch, regardless of argument order. Providing existing `fsck.cpm` and `fsed.cpm` executables does **not** add GUI checker/repair/editor features.

## v0.9 — Qualification and packaging

- Run filesystem, geometry, sector-ordering, RAW interoperability, container, sidecar, system-track, and cross-platform regressions.
- Validate source preservation, interruption/recovery, error handling, CLI output/exit behavior, and package relocation.
- Establish and test the supported OS/CPU release matrix. Existing ARM64 macOS/Windows/Linux evidence is a starting point, not proof of x86-64 compatibility.
- Produce self-contained packages with required runtime dependencies, cpmtools executables, licensing, and corresponding-source materials.
- Complete documentation and release-readiness checks.

**Acceptance:** Release targets have reproducible builds, qualified installers/packages, and documented evidence of the required workflows.

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
