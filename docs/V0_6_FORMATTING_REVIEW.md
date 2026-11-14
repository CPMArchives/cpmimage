# v0.6 image creation and formatting review

New Image uses the catalogue picker to initialize a full-size private RAW working image. Save selects RAW, supported standard CPCEMU DSK, IMD, JV3, or DMK. Existing structured encoding and mapping checks run before journaled publication. Format Image asks for confirmation, uses the current definition and capacity, and preserves reserved bytes by default. Both workflows accept optional system-track data with explicit E5 padding. Discard restores the previous saved image or closes an unpublished image.

The native helper reuses the pinned mkfs initializer. The standalone cpmtools patches correct the bootsec assertion and route initialized directory blocks through cpmSync. Sequential boot bytes use the image offset and the resolved physical reserved area. A skew permutation crossing a partial physical reserved boundary is rejected. CP/M Plus label and timestamp records use upstream construction. Boot-derived amstrad initialization remains unsupported. Structured image creation retains v0.5 mapping restrictions. The working limit remains 128 MiB.

## Evidence

- macOS ARM64 passed standalone formatter placement checks on a clean cpmtools 2.23 extraction with only the two formatter corrections. Tests inspected actual label and timestamp placement, reserved boot bytes, byte offsets, and every untouched byte.
- macOS ARM64 passed broker creation and reformatting for RAW, standard CPCEMU DSK, IMD, JV3, and DMK, with CP/M Plus label, timestamp and optional boot data verified on RAW. Unmodified cpmtools independently imported and extracted the expected bytes from a newly initialized IBM 3740 image. Discard restored a populated directory.
- Seven journaled Save regressions and the existing macOS Qt suite passed.
- trs80gp on macOS ARM64 booted stock Montezuma Micro CP/M 2.31 and read the expected proof text from a newly initialized 40-cylinder, single-sided DMK in drive B.
- Windows 11 ARM64 and Debian 13 ARM64 passed focused Qt startup and format selection and broker creation/reformat smoke across the five containers. These guest builds preceded the final narrow error-handling change.
- x86-64 and manual native GUI walkthroughs were not tested.

## Partial-reservation review follow-up

Both formatter entry points call and check cpmReadSuper before the partial-reservation check. It generates the skew table when the definition omits one, retaining the configured skew factor and using identity for the default factor of one. The regression combines bootsec 11, no explicit skew table, a 17-byte offset, sequential boot data, and CP/M Plus metadata. It exposed an early LibDsk geometry setup on the helper's unopened device. The helper now defers that setup until mkfs opens its byte-offset RAW device. Clean standalone formatter checks, nine initialization/catalogue checks, and seven journaled Save checks passed on macOS ARM64.
