# v0.3 disk definitions and format selection

## Using the catalogue

Open an image and choose a definition from ranked candidates or switch to the complete catalogue. Search matches identifiers and classification metadata. Favorites persist across runs. Settings → Format favorites shows saved entries, including entries absent from the current catalogue. Click the format button beside an open image to reinterpret the source. A failed interpretation leaves the source selected and disables export until a definition successfully reads it.

File → Load diskdefs catalogue replaces the bundled catalogue for the current run. The equivalent launch option is `/diskdefs=PATH` or `--diskdefs=PATH`. An external catalogue may redefine identifiers or contain a smaller set. File → Use bundled diskdefs restores the catalogue shipped with the app. Load failures leave the last valid catalogue active.

For startup image opening, use `/img=PATH` or `--image=PATH`. An explicit `/f=FORMAT` or `--format=FORMAT` for that image takes precedence over a matching FDF. Otherwise, the application uses a valid matching FDF, a remembered association whose image and definition still match, or the picker. An FDF must contain one complete definition; image stem matching is exact and the FDF extension is case-insensitive. Diagnostics for invalid FDFs appear before the fallback picker or remembered choice. Opening and switching formats never create or edit a sidecar.

## Implementation and limits

The Qt → Python → C → cpmtools/LibDsk process arrangement remains in use. The broker adds `catalogue` and `inspect` requests to protocol v1. The optional `source_sha256` and `definition_sha256` fields on `open` bind a selection to the bytes inspected. The native helper uses the cpmtools parser to report significant geometry and uses its filesystem code for directory listing and export. The original catalogue text ships with separate display metadata. The pinned catalogue's commented-out TRSI terminator is recovered at the next definition boundary. cpmtools 2.23 has no diskdefs inheritance syntax. The native reader handles the built-in Amstrad boot-sector definition for manual selection.

RAW interpretation supports validated geometry, offsets, skew tables, boot sectors, and CP/M allocation and directory variants. Standard CPCEMU DSK retains the existing qualified single-sided container scope and is decoded by LibDsk into RAW. Limits include 128 MiB working images and 4,096 returned files. Definitions outside those limits can be listed, and image operations may reject them. Classification is curated; other definitions appear as unclassified.

Candidate discovery uses exact RAW geometry or the supported standard DSK header, followed by the native directory check. Nonempty directories rank before empty directories; equal evidence ties sort by identifier. Trimmed images, offset layouts with trailing data, and boot-derived layouts may require manual selection. Broader candidate discovery and evidence thresholds belong to v0.8. A plausible interpretation never proves historical identity or converts the source. Save / Discard / Cancel remains the required policy when writing arrives in v0.4.

## Validation

| Environment | Result |
| --- | --- |
| macOS 27.2 ARM64, Qt 6.12.0 | Passed 27 Qt checks, 4 catalogue and sidecar checks, 11 broker regressions, six final-track cases, and representative comparisons against independently built unmodified cpmtools 2.23. |
| Windows 11 ARM64, Qt 6.12.0 / MSVC 2022 | Passed app startup diagnostics and focused Qt catalogue, format selection, remembered selection and failed-switch smoke. The native helper was cross-built for ARM64 with LLVM-MinGW. |
| Debian 13.7 ARM64, Qt 6.12.0 / GCC 14.2 | Passed app startup diagnostics and focused Qt catalogue, format selection, remembered selection and failed-switch smoke in UTM. |
| Windows x86-64, Linux x86-64, macOS x86-64 | Not tested. |

The Qt tests use the offscreen platform plugin. Native desktop appearance and release package qualification are outside this smoke run.
