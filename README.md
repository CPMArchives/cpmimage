# CP/M Image

CP/M Image is a graphical file manager for CP/M disk images on macOS, Windows, and Linux. It bundles cpmtools for command-line use.

## Download v1.1

Get the five platform packages, corresponding source, and SHA-256 checksums from the [v1.1 release downloads](https://github.com/CPMArchives/cpmimage/releases/tag/v1.1#assets). The release notes cover the changes and supported formats.

- **macOS ARM64 (Apple silicon):** Open the DMG and drag **CPMImage.app** to **Applications**.
- **Windows ARM64 and x86-64:** Download the executable for your architecture and run it.
- **Linux ARM64 and x86-64:** Download the executable for your architecture, mark it executable with `chmod +x`, and run it.

## What you can do

- Open RAW, IMD, JV3, DMK, and supported standard CPCEMU DSK images. Some structured layouts and conversions require a physical mapping profile.
- Browse user areas and export individual files, selections, or entire user areas. The file list shows record counts and the footer shows free filesystem space.
- Import, rename, delete, and move files between user areas, edit supported attributes, and transfer files between open images.
- Create or format images using a selected disk definition and manage reserved system tracks. Edits stay in a private working copy until Save or Save As.
- Choose a disk definition from the bundled cpmtools catalogue or supply your own definitions. The format picker can suggest candidates and accepts manual selection.

## File and archive viewer

- Select a file and press **V** or **Enter**, or double-click it to open the read-only viewer. Press **T** to switch between text and hex. In text mode, press **7** to toggle stripping the high bit for WordStar-style text.
- Preview supported Squeeze (SQ) and Crunch (CR) files. After successful decompression, switch between the original and decompressed bytes.
- Browse LBR and ARK archives, including nested archives. Use **Up/Down** to select a member, **V** to view it, and **Esc** to return to the parent archive.

See [CP/M Image Help](docs/HELP.md) for everyday workflows. The same guide is available offline from the application's **Help** menu.

## Command line

Run the package executable with a utility selector. On macOS, use the executable inside the installed app:

```sh
/Applications/CPMImage.app/Contents/MacOS/cpmimage /cpmls -f ibm-3740 disk.raw
```

On Windows or Linux, pass the same arguments to the downloaded executable. Selectors include `/cpmls`, `/cpmrm`, `/cpmcp`, `/cpmchmod`, `/cpmchattr`, `/mkfs.cpm`, `/fsck.cpm`, and `/fsed.cpm`. Use `/diskdefs=PATH` to override the bundled catalogue.

## Build and packaging

CP/M Image uses Qt 6 Widgets and C++17, a separate Python broker, and a native helper built from pinned cpmtools 2.23 and LibDsk 1.4.2 sources. See the [build instructions](docs/BUILDING.md), [engineering specification](docs/ENGINEERING_SPEC.md), and [roadmap](docs/ROADMAP.md).

The committed fixtures are independently generated test data. Private VM state, downloaded dependencies, toolchain binaries, and generated application packages are excluded from Git. Redistributable binary packages include corresponding source and third-party notices.

For packaging, apply `packaging/scripts/configure-utility-catalogue.py` to a
pinned cpmtools 2.23 source tree before compiling the standalone utilities.
The script applies the v0.6 formatter fixes and requires `patch` on PATH.
Build the complete suite with a curses implementation for `fsed.cpm` and
set `CPMIMAGE_CPMTOOLS_BIN` to the native build output when assembling the
package. Include any Windows utility runtime DLLs in that directory.

For the Windows ARM64 RAW utility build, extract cpmtools 2.23 and PDCurses 3.9
into sibling directories under a scratch directory. From an ARM64 MSVC
developer shell, run `python packaging/scripts/build-utilities-msvc.py SCRATCH`
and set `CPMIMAGE_CPMTOOLS_BIN` to `SCRATCH/bin`. The Windows console editor
links PDCurses statically and the utilities share the package's MSVC runtime.
This recipe uses the upstream RAW file backend. It has no LibDsk container
options. Upstream cpmtools 2.23 opens images read-only in `fsed.cpm`.

## License

Project code is **GPL-3.0-or-later**. See [LICENSE](LICENSE). Third-party components retain their own licenses. See [Licensing and third-party notices](docs/LICENSING.md).
