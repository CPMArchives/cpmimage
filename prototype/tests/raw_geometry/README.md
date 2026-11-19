# RAW bridge regression

The generator is preserved from CPMArchives/rcpm-cpmsim commit d0e08be,
repro/cpmtools-raw-geometry/generate.py. All image contents are synthetic.
The checker adds Workbench write/read and explicit-format coverage.

Run the generator with an unused output directory, then run the checker
with that directory and --tools PATH --posix PATH.
Build the POSIX comparison from unmodified cpmtools 2.23 using plain
./configure followed by make cpmcp cpmrm. Build the patched suite with the
Workbench patch runner and pinned LibDsk 1.4.2. Both builds must be native.

The fixtures total roughly 520 MiB logically. The large fixture is sparse.
The checker uses private copies and verifies implicit and explicit RAW
extraction, file replacement through remove/import, extraction by both
backends, full-image equality with POSIX writes, and raw,ibm360 plus
libdsk:format ibm360 selection paths.

Only the CLI exercises the 512 MiB fixture. The GUI working-image limit
remains 128 MiB.

For explicit non-linear ordering, compile explicit_geometry.c against the
patched device_libdsk.o and the pinned LibDsk headers/library. Run it on a
private 737,280-byte RAW image whose 512-byte sectors contain distinguishable
patterns. It checks both explicit ibm720 paths retain OUTBACK ordering and
verifies the physical offsets for reads and writes. The test modifies that
private file.
