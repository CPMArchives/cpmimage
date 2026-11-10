# cpmimage — Project Proposal

> Historical initial proposal. See [current prototype scope](READ_ONLY_PROTOTYPE.md) and [validation](VALIDATION.md) for implemented behavior and [licensing](LICENSING.md) for the owner-approved source license.

Status: Initial proposal. Architecture and release scope require feasibility validation.

## Purpose and audience

cpmimage will be an open-source, cross-platform graphical disk-image manager specializing in CP/M filesystems. It will target macOS, Windows, and Linux and provide WinImage-style disk-management workflows without requiring users to operate cpmtools from the command line.

The intended audience includes CP/M users, software archivists, emulator users, and developers who need to inspect, populate, extract, and maintain disk images.

The project will be independent of BetterCP/M. BetterCP/M's cpmsim, TRS-80 Model II, and Model 4 images will be useful initial compatibility test cases, but will not define the limits of supported CP/M systems.

## Design references

| Reference | Intended influence |
| --- | --- |
| WinImage | General interface, usability, and disk-image workflows |
| SteinBlume | CP/M-specific functionality and filesystem handling |
| cpmtools | Proposed primary filesystem backend and diskdefs ecosystem |
| libdsk | Candidate backend for additional image-container formats |

These are design references and candidate dependencies. Their capabilities, integration interfaces, licensing, and redistribution requirements must be verified during feasibility work. Referencing an application does not imply reusing its source or assets.

## Initial release objectives

| Feature | Priority |
| --- | --- |
| Open and inspect CP/M disk images | Essential |
| Browse user areas 0–15 | Essential |
| Import and export files | Essential |
| Delete and rename files | Essential |
| Display file attributes and disk statistics | Essential |
| Select formats from diskdefs | Essential |
| Automatic format detection using diskdefs | Essential |
| Create new disk images | Essential |
| Save modified images safely | Essential |
| Drag-and-drop operations | Desirable |
| Copy files between images | Desirable |

Automatic detection is an essential workflow, not a guarantee that every image can be uniquely identified. Desirable features may be deferred if they would delay reliable essential operations.

File attributes and statistics will reflect the selected filesystem's supported semantics. Import, export, and rename workflows must address CP/M naming limits, user areas, host filename collisions, and overwrite choices.

Creating an image means creating a usable filesystem image for a supported format. Generating bootable system disks or installing an operating system is outside the initial scope.

## Working architecture

| Component | Proposed direction |
| --- | --- |
| Graphical interface | Qt 6 Widgets |
| Implementation language | C++17 or C++20; exact baseline to be selected during feasibility work |
| Build system | CMake |
| CP/M filesystem operations | cpmtools |
| Image-container operations | libdsk where justified and supported |
| Format definitions | Dynamic loading of diskdefs, including user-defined formats |
| Format identification | Separate detection subsystem |

The backend integration strategy is provisional. A technical proof of concept must compare invoking cpmtools executables with linking or adapting its filesystem implementation before choosing an approach.

The application should distinguish GUI workflows, filesystem operations, image-container access, format definitions, and detection logic. This separation should support testing and backend decisions without introducing a general plugin framework before one is needed.

Filesystem format and image-container format are separate concerns. A diskdefs entry is not, by itself, a promise that every container carrying that filesystem can be read, modified, or created. Supported combinations will be documented explicitly.

## Format identification

Format identification is a distinctive project capability. The proposed detector will combine:

- Available image-container metadata.
- Image size and physical geometry where known.
- Candidate filesystem definitions from diskdefs.
- Filesystem validation.
- Ranked candidate results with reasons for the ranking.

Candidate validation should investigate directory-entry plausibility, extent and allocation consistency, and bounds checks. These are proposed investigation areas, not an approved algorithm.

Users must always be able to select a format manually. The interface must distinguish a strong match, an ambiguous set of candidates, and no plausible match. It must not present an uncalibrated score as a statistical probability.

Some CP/M formats may be indistinguishable from image contents alone. Empty images, damaged directories, overlapping definitions, and missing geometry information should be included in the investigation. Detection must not modify an image.

A ranked result is evidence to help select a format; it does not establish that writing with that format is safe. The engineering specification will define when selection or confirmation is required before editing.

## Write safety

Safe saving is an initial-release requirement; its mechanism remains open.

The feasibility work will compare temporary working images, transactional saves, and other suitable mechanisms. Evaluation must cover preservation of the original image after failed operations, cancellation, read-only sources, save failures, and the preservation of container metadata or system tracks outside the filesystem.

Unsupported or unverified write combinations should remain read-only. The release must document read, write, and creation support separately. No universal write support is promised.

## Development phases

### Phase 1 — Feasibility and engineering investigation

- Evaluate cpmtools integration approaches on all three target operating systems.
- Evaluate whether libdsk is needed for the initial supported container set.
- Establish a small, representative image corpus with known formats and expected contents.
- Prototype candidate enumeration and filesystem validation for detection.
- Demonstrate a safe edit-and-save workflow.
- Review dependency licenses, packaging, and redistribution constraints.
- Record findings, tradeoffs, and unresolved limitations.

Deliverables: feasibility findings, an initial format-support matrix, and an engineering specification informed by working experiments.

### Phase 2 — Essential application workflows

Implement image opening, manual format selection, detection results, user-area browsing, file operations, image creation, statistics, and safe saving for the validated support matrix.

Deliverable: a usable initial application with documented limitations.

### Phase 3 — Initial release qualification

Validate the essential workflows on macOS, Windows, and Linux; verify successful saves and failure handling; prepare installation instructions, user documentation, and platform packages.

Add desirable features only where proportionate. No release date is established by this proposal.

## Validation direction

Use known-good images and expected file contents to verify browsing, export, import, rename, deletion, and image creation. Reopen saved images and check that intended changes persist and unrelated content remains intact.

The corpus should include ambiguous formats, empty images, malformed or truncated images, read-only inputs, and representative supported containers. BetterCP/M images provide initial practical cases; additional systems are needed to avoid fitting behavior solely to BetterCP/M.

The engineering specification will define exact acceptance criteria, fixtures, and failure cases. This proposal does not authorize a broad implementation or claim that backend compatibility has already been demonstrated.

## Deferred roadmap

- Graphical disk-allocation maps.
- Deleted-file recovery.
- Hexadecimal and text viewers.
- Additional image-container formats.
- Image conversion.
- Physical floppy-drive access, subject to feasibility.

These are future investigation directions, not initial-release commitments. Image conversion must distinguish container conversion from copying files into a different filesystem layout. Physical-drive access will require a separate assessment of operating-system, hardware, driver, and safety constraints.

## Open engineering decisions

| Question | Required investigation |
| --- | --- |
| Backend integration | Compare executable invocation with direct integration for portability, error reporting, maintenance, distribution, and licensing |
| Image support | Establish which raw and structured formats can reliably be read, written, and created; document each capability separately |
| Format detection | Define validation rules, ranking, ambiguity handling, and confidence thresholds using a representative corpus |
| Write safety | Select a mechanism that preserves source images through failed edits and saves, including container-specific constraints |

User-defined diskdefs loading also needs explicit rules for discovery, precedence, validation, and reporting conflicting definitions.

## Relationship to the engineering specification

This proposal establishes purpose, intended capabilities, architectural direction, boundaries, and development phases. It is not a final engineering specification.

After feasibility testing, the engineering specification will define interfaces, backend behavior, supported formats, detection algorithms, error handling, save behavior, packaging, and acceptance criteria.

## Licensing

The project is intended to be open source. A project license has not yet been selected. License selection must account for the chosen integration and distribution of Qt, cpmtools, libdsk, and any reused definitions or other material. No license compatibility is assumed by this proposal.
