# Build and package the read-only prototype

Run commands from the repository root. Python 3.9+, CMake 3.21+, a C++17 compiler and Qt 6.7+ Widgets/Test are development prerequisites. Qualification used Qt 6.12.0. The v0.1 runtime is the baseline; the v0.2 navigation review changes the GUI while retaining the read-only broker/helper. See `V0_2_NAVIGATION_REVIEW.md` for the current validation scope. Downloaded sources, build products, native toolchains and VM state are ignored by Git.

## macOS accessibility requirement

Qt 6.12.0 has a native Cocoa accessibility crash when selected table cells are queried (QTBUG-149612). Offscreen test success does not establish native safety. For the v0.2 native UI, use the private source build below: `build-qt-unix.sh macos` applies the recorded upstream fix. Keep the application and platform plugin on the same Qt build; do not mix Homebrew and private runtimes. Homebrew Qt itself is not patched by these scripts. See [the local review findings](V0_2_NAVIGATION_REVIEW.md).

## macOS developer build

Install Apple command-line developer tools, CMake and Qt. The pinned backend inputs are about 1.2 MB; they are downloaded from their official sites and verified before use.

```sh
python3 packaging/scripts/fetch-dependencies.py --group backend
sh packaging/scripts/build-helper-macos.sh
cmake -S prototype -B build/gui -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build/gui -j2
export CPMIMAGE_ROOT="$PWD"
export CPMIMAGE_HELPER="$PWD/packaging/cpmimage-helper-macos"
export CPMIMAGE_PYTHON="$(command -v python3)"
python3 -I -B prototype/tests/broker_check.py
python3 -I -B prototype/tests/final_track_check.py --root "$PWD" \
  --helper "$CPMIMAGE_HELPER" --report /tmp/cpmimage-final-track.json
ctest --test-dir build/gui --output-on-failure --timeout 90
build/gui/cpmimage-review.app/Contents/MacOS/cpmimage-review \
  --root "$PWD" --python "$CPMIMAGE_PYTHON" --helper "$CPMIMAGE_HELPER"
```

The helper build applies the recorded cpmtools bridge patch. It builds a private shared libdsk; no Homebrew libdsk is required by that helper recipe. A developer Qt installation is sufficient for testing, but not a relocatable binary release. Never package arbitrary Homebrew dependencies without rebuilding the corresponding notice/source inventory.

## Debian developer build

Install build-essential, CMake, Python and Qt6 development packages. `prototype/build-linux.sh` contains the fresh native helper/libdsk build, including the glibc header compatibility option, bridge patch, GUI build and tests:

```sh
python3 packaging/scripts/fetch-dependencies.py --group backend
sh prototype/build-linux.sh
```

It builds the GUI under `prototype/build/qt`. For a previously configured backend tree, `packaging/scripts/build-helper-linux.sh` rebuilds just the patched read-only helper. Set the three `CPMIMAGE_*` variables as above, using `prototype/build/helper-readonly`, to run the extra final-track suite.

## Private runtimes and relocatable packages

`packaging/dependencies.json` pins the exact source/binary inputs by URL and SHA-256. `fetch-dependencies.py --verify-only` checks cached files without downloading. Source archives are intentionally not committed; binary distribution must carry the matching corresponding-source companion generated below.

```sh
python3 packaging/scripts/fetch-dependencies.py --group runtime
sh packaging/scripts/build-qt-unix.sh macos  # use linux on Debian
sh packaging/scripts/build-python.sh macos  # use linux on Debian
```

Build the GUI against the private Qt installation:

```sh
cmake -S prototype -B packaging/app-macos -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/packaging/qt-macos/install" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
cmake --build packaging/app-macos -j2
python3 packaging/scripts/package-macos.py
python3 packaging/scripts/audit-macos.py
```

For Linux substitute `app-linux` and `qt-linux/install`, omit the macOS deployment flag, then run `package-linux.py` and `audit-linux.py`. The native Linux helper is taken from `prototype/build/helper-readonly`. Linux packaging needs `patchelf` and the X11/XCB, OpenGL/EGL, fontconfig and xkbcommon development/runtime stack. See the explicit XCB requirement in `build-qt-unix.sh`; a Qt build lacking XCB must not be called a desktop package. The minimal qualification build disables the D-Bus accessibility bridge, which remains a release limitation.

Notice generation reads the pinned Qt/Python source trees extracted by those build recipes. For Windows assembly on a host not building Unix runtimes, unpack the fetched Qt archive under `packaging/qt-source` and Python under `packaging/build-python-linux` first, so the same upstream licenses/attributions can be read.

Mac packages use ad-hoc signatures and target macOS 14.4, but execution has only been qualified on macOS 27.2. They are not notarized. Linux packages were tested on Debian 13 ARM64; they are not universal Linux binaries.

## Windows ARM64 qualification recipe

The GUI uses official Qt6.12 MSVC2022 ARM64 and native MSVC19.44/SDK10.0.26100. The isolated C helper remains cross-built by LLVM-MinGW23; no C++ ABI crosses the process boundary.

1. `fetch-dependencies.py --group windows` fetches the exact Qt, portable CMake, embedded Python and LLVM-MinGW inputs. Extract LLVM-MinGW under `windows-runtime` and the Python embed ZIP into `windows-runtime/offline-media/CPMImageTests/python`. Extract the Qt7z archive into `packaging/qt-msvc` using sevenzip, without an extra top-level directory.
2. On the Mac build host, run `build-helper-windows.sh`. It applies the same bridge patch and emits `prototype/build-windows/helper-readonly.exe`.
3. For the original offline native kit recipe, `fetch-msvc.py` and `fetch-sdk-cabs.py` download only the pinned public payload records in `packaging/toolchain-inputs`. `extract-msvc.py` uses ZIP extraction and `msiextract` (msitools) without installing an IDE. `stage-msvc-media.py` and `zip-msvc-kit.py` assemble the portable development kit. Each developer must review and accept the applicable Microsoft terms independently; these scripts neither establish a license nor accept an agreement.
4. Extract that kit as `C:\CPMImageMSVC`. Run `stage-app-sources.py --helper prototype/build-windows/helper-readonly.exe` and extract the source ZIP under `C:\CPMImageSource`; the fixed staging prefix is `cpmimage-feasibility` regardless of the checkout name. Copy/extract embedded Python under that root's `windows-runtime/offline-media/CPMImageTests/python` directory.
5. Run `build-msvc.cmd` in Windows. `CPMIMAGE_ROOT`, `CPMIMAGE_HELPER` and `CPMIMAGE_PYTHON` can override the default source/helper/interpreter paths. The kit paths, release CRT import libraries, native ARM64 resource tools and Release/MD probe configuration are intentionally pinned.
6. Pull the resulting `cpmimage-review.exe` back to the packaging host and run `package-windows.py /path/to/cpmimage-review.exe`, then `audit-windows.py` (requires the pinned LLVM readobj tool).

The complete Windows kit acquisition/extraction sequence was not rerun. The v0.2 navigation/icon source was rebuilt and tested natively using the existing offline ARM64 kit; see `V0_2_NAVIGATION_REVIEW.md`. Prior v0.1 packaging qualification is recorded separately from the v0.2 GUI changes. The committed Microsoft payload plans replace reliance on a mutable live catalog. No SDK/compiler/IDE binaries enter the product or source companion.

## Package verification and corresponding source

```sh
python3 packaging/scripts/check-package.py /absolute/path/to/packaged-executable \
  --fixture "$PWD/fixtures/ibm3740-independent.raw" \
  --final-track-manifest "$PWD/fixtures/final-track-manifest.json" \
  --report /tmp/cpmimage-package-check.json
```

Repeat after moving the whole bundle to a path containing spaces/Unicode and hiding developer runtime prefixes. The checker removes developer search paths, poisons Python environment variables, checks exact exports, no overwrite, rapid exports and source/package immutability. It uses the actual package's broker/helper/private interpreter.

After committing reviewed source, `archive-review.py --source-only` creates the source companion from tracked source plus the four verified cpmtools/libdsk/Qt/Python archives. It rejects a dirty tracked tree. Available runtime configuration/SBOM records are included. Without `--source-only`, collect all three platform product directories under `packaging/dist` first; the script also archives them and writes SHA256SUMS. ZIP creation preserves framework symlinks and executable permissions.

Do not distribute binaries without that matching companion and the generated notices. Developer toolchains remain excluded. This milestone does not implement a public release, CI matrix, installer, signing identity, or notarization workflow.
