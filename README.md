# cpmimage

A cross-platform CP/M disk-image GUI for macOS, Windows and Linux.

## Current status

The current development milestone is v0.3. The read-only application browses and exports files from CP/M images using bundled, external, and image-specific disk definitions. Published packages are available on the [releases page](https://github.com/CPMArchives/cpmimage/releases).

The application uses Qt 6 Widgets/C++17, a separate Python broker, and a read-only C helper built from pinned cpmtools 2.23 and libdsk 1.4.2 sources.

- [v0.3 implementation and validation](docs/V0_3_DISKDEFS_REVIEW.md)
- [Build and run](docs/BUILDING.md)
- [Prototype behavior and limits](docs/READ_ONLY_PROTOTYPE.md)
- [Batch and user-area export review](docs/V0_2_BATCH_EXPORT_REVIEW.md)
- [Compact toolbar and image information review](docs/V0_2_TOOLBAR_REVIEW.md)
- [Validation evidence](docs/VALIDATION.md)
- [Owner testing notes and backlog](docs/USER_TESTING_BACKLOG.md)
- [Complete SIG/M filename audit, including DOS](docs/CATALOG_FILENAME_AUDIT.md)
- [Licensing and third-party notices](docs/LICENSING.md)
- [Initial project proposal](docs/PROJECT_PROPOSAL.md) (historical goals, not implemented scope)

## Verified milestone

Native ARM64 macOS, Windows 11 and Debian 13 validation covers listing/export, cancellation and reader failures, source-image preservation, and portable runtime packaging. The cpmtools/libdsk geometry bridge is patched to preserve odd track counts: its old fallback silently reduced 77 tracks to 76. Final-track exports pass on all three platforms.

The committed fixtures are independently generated test data. SIG/M images, BetterCP/M system images, downloaded dependencies, private VM state, toolchain binaries and generated application packages are excluded from Git. Dependency downloads are pinned by SHA-256. Creating a redistributable binary package also requires its corresponding-source companion and third-party notices; source links alone are not the proposed distribution method.

## License

New project code is **GPL-3.0-or-later**, as approved by the owner. See [LICENSE](LICENSE). Third-party code and documentation retain their own licenses; see [licenses/third-party](licenses/third-party).

### Headless cpmtools

Use the package executable with a utility selector such as
`cpmimage /cpmls -f ibm-3740 image.raw`. On macOS, run
`CPMImage.app/Contents/MacOS/cpmimage-review` from Terminal. Selectors include
`/cpmls`, `/cpmrm`, `/cpmcp`, `/cpmchmod`, `/cpmchattr`, `/mkfs.cpm`,
`/fsck.cpm`, and `/fsed.cpm`. One selector may appear anywhere in the
argument list. Use `/diskdefs=PATH` to override the bundled catalogue.

For packaging, apply `packaging/scripts/configure-utility-catalogue.py` to a
pinned cpmtools 2.23 source tree before compiling the standalone utilities.
Build the complete suite with a curses implementation for `fsed.cpm` and
set `CPMIMAGE_CPMTOOLS_BIN` to the native build output when assembling the
package. Include any Windows utility runtime DLLs in that directory.
