# cpmtools RAW geometry correction

Investigation date: 2026-11-19. Baseline is released Workbench v2.1,
commit e20f9095d156e26f5d8e9b24c5d671ee20f2bfd0. Test host is macOS ARM64. Windows and Linux runtime matrices were not repeated for this portable bridge change. Backend identification used the staged release executables; Linux ARM64 was also checked in its native VM.

## Dependency and code review

Workbench pins cpmtools 2.23 and LibDsk 1.4.2. Its standalone cpmtools use
device_libdsk.c. Existing local patches preserve odd track counts, validate
disk definitions, provide a dedicated linear RAW working device and writes,
retain CP/M filename data in the helper, expose diagnostic directory reads,
and correct formatter addressing. LibDsk carries the TeleDisk CRC and
sector-inventory correction.

The helper replaces raw with cpmimage-raw for linear byte-offset I/O.
The macOS standalone /cpmcp -T raw uses LibDsk and is affected. The Linux x86-64 package also links libdsk.so.3. The Windows ARM64/x86-64 and Linux ARM64 standalone cpmtools contain the POSIX backend and avoid this specific inference path; their GUI helpers still use the separate working device. The correction applies
only after LibDsk selects the raw driver without an explicit format suffix.
It clears inferred geometry, selects SIDES_ALT and sector base 1, then lets
diskdefs supply the remaining geometry. Explicit raw,FORMAT still calls
lookupFormat. The libdsk:format diskdef parameter still overrides geometry
in Device_setGeometry. The working device and other container drivers
retain their existing paths.

## Released executable reproduction

Ran the actual v2.1 macOS cpmwb /cpmcp -T raw with generated diskdefs.

| Fixture | Exit | Exact expected bytes |
| --- | ---: | --- |
| 4 MiB failing, VOLUME.TXT | 0 | Yes |
| 4 MiB control, TOLUME.TXT | 0 | Yes |
| 512 MiB failing | 0 | No |

The 4 MiB definition has 255 tracks. Workbench's odd-track correction forces
one head and masks the sidedness error for that case. The even-track
512 MiB fixture retains the harmful inferred side ordering. Its extracted
SHA-256 was 905bb3b622e58a5916eec1fcfed908919e46dd53598a70085fd78e9c93a947cf.
The expected value is a55796ca77e68d0f13750b308cf44d1e22bbc179914b424bf8492c708b685d4b.

## Correction and verification

Applied the RCP/M candidate through the shared patch runner and rebuilt the
macOS CLI suite and helper. A second runner invocation passed idempotently.

- All three fixtures extract exact bytes with implicit detection and -T raw.
- Remove/import replacement and extraction pass for all three fixtures.
  Unmodified cpmtools 2.23 with POSIX independently reads the written files.
  Complete modified images match the POSIX writer byte-for-byte.
- raw,ibm360 and libdsk:format ibm360 pass initialization, import, extraction,
  and independent POSIX extraction.
- All three fixtures extract correctly through a local copy of the app
  with the corrected bundled CLI suite and helper installed.

- Explicit ibm720 geometry (SIDES_OUTBACK) was tested through both selection
  paths at the device layer. The geometry survives, and logical track 1 reads
  and writes the expected physical RAW track 2. The new correction does not
  force explicitly requested OUTBACK images into linear ordering.
- Nine existing container-save tests passed with the rebuilt helper, including
  IMD/JV3 direct extraction by the patched standalone cpmtools and DMK RAW
  comparison. Twenty initialization/catalogue tests passed with independent
  POSIX extraction, covering RAW, IMD, JV3, DMK, and standard CPCEMU DSK.
- A completely E5-filled RAW image still fails LibDsk automatic driver
  detection without -T raw. Both the released and corrected builds reproduce
  this pre-existing limitation. Explicit -T raw succeeds. This is driver
  detection before Device_open's geometry selection, outside this correction.

These checks cover selected RAW layouts. The 512 MiB fixture is a CLI test
and does not change the GUI's 128 MiB working-image limit.

## Provenance and upstream status

[Original diagnosis and candidate](https://github.com/CPMArchives/rcpm-cpmsim/tree/d0e08be/repro/cpmtools-raw-geometry).
The candidate raw-linear-order.patch is retained as
packaging/source/cpmtools-2.23-raw-linear-order.patch. The original synthetic
generator is preserved with attribution under prototype/tests/raw_geometry.
The write checker is Workbench regression code.

This is a local cpmtools/LibDsk integration correction. No upstream report
has been submitted as of 2026-11-19. Later upstream versions were not
assessed. Dependency versions remain pinned.
