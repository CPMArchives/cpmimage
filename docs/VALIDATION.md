# Validation of the read-only milestone

## Preserved qualified implementation

The code in `prototype/src`, the broker/helper guard, the native helper source, runtime adapter/supervisor and cpmtools bridge patch are preserved byte-for-byte from the owner's validated local prototype. Filename/UI feedback is recorded in the backlog; it is not silently presented as a tested implementation change.

The records in [validation/2026-11-10](validation/2026-11-10) come from the original native ARM64 qualification environments. Developer-machine paths were normalized to placeholders. `provenance.json` records hashes before and after that normalization; outcomes and payload hashes were not changed.

| Platform | Native helper | GUI | Result |
|---|---|---|---|
| macOS27.2 ARM64 | Apple Clang; shared libdsk1.4.2 | private Qt6.12 |6 helper/broker final-track scenarios,11 broker checks,12 functional Qt cases pass |
| Windows11 ARM64 | LLVM-MinGW23 C helper; static libdsk1.4.2 | official Qt6.12/MSVC2022 |6 final-track scenarios,9 broker checks+2 POSIX-only skips,12 functional Qt cases pass |
| Debian13 ARM64 | GCC14.2; shared libdsk1.4.2 | private Qt6.12 |6 final-track scenarios,11 broker checks,12 functional Qt cases pass |

Qt reports14 passes when setup/cleanup are counted. Windows's Job Object process-tree checks are in the GUI suite; the two skipped broker cases use POSIX-only fixtures. The Windows GUI was validated with MSVC; the isolated C helper was not compiled with MSVC.

## Final-track regression

The old cpmtools/libdsk bridge converted77 requested logical tracks into38 cylinders×2heads=76 tracks after an unhelpful raw probe. It could list early directory sectors while failing to export data on track76. The recorded patch preserves odd logical track counts as single-sided while retaining explicit/successful geometry precedence.

The six new synthetic cases cover the final physical sector, a valid short IBM3740 image, the77-track ModelII layout, and the even-track CPMSIM, Model4 and Ampro layouts. The unpatched helper fails the three odd-track cases on every tested platform; the patched helper and broker pass all six. Exact payload hashes are in `fixtures/final-track-manifest.json`.

A read-only local scan of310 SIG/M images found70 files in64 images reaching track76. Every one failed with the old helper and matched independent POSIX cpmtools exports after the fix. The image bytes were unchanged. This establishes export correctness against those images, not historical completeness of their contents; separate SIG/M issues track the source-library over-capacity and image-length problems.

## Packaged runtime checks

The same six scenarios passed through the actual packaged broker/native helper/private Python on every platform. Packages were moved to paths with spaces/Unicode; Windows/Linux were checked at two locations with developer Qt hidden on the second pass (Linux also hid developer Python/libdsk). Empty executable search paths and poisoned Python environment variables did not affect operation.

The common package checker also verified three existing payload oracles, overwrite refusal,20 rapid exports, and source/package immutability. The Mac binary/signature audit covered61 Mach-O files, Windows28 native ARM64 PE files, and Linux64 ELF files. Mac uses ad-hoc signing, Windows is unsigned; no public release was made.

## Fresh source-checkout replay

During preparation of this proposed commit, the helper/libdsk were rebuilt from the verified pinned archives in a fresh checkout, the GUI was rebuilt against the installed Qt6.12 developer package, and the following were rerun on macOS:

- 11 broker checks;
- 12 functional Qt cases (14 with lifecycle);
- all6 final-track helper/broker cases;
- byte-for-byte regeneration of all4 baseline fixtures;
- the complete catalogue scanner (6,681 entries,310 volumes, no unparsed rows).

The runtime source identity check is recorded alongside the fresh replay. Dependency/backend official download URLs and hashes were also verified. Packaging-script preparation adds pinned input fetching, a checkout-name-independent staging prefix, portable archive creation and removes task-specific license-acceptance recording. The full Microsoft development-kit acquisition and all private-runtime builds were not repeated from scratch for those script-only cleanups. Prior platform runtime/package evidence is distinguished from this fresh source replay.

## Limits

ARM64 only. No x64/x86 or older-macOS execution qualification. The14.4 Mac target is not a tested minimum. Linux distribution portability and D-Bus accessibility need further work. The old even-track fallback heuristic remains; there is no general detector. GUI tests run offscreen; the owner separately confirmed the native Mac application opens/lists real images after the fix. No new write workflow, import mapping, multi-selection or tabbed architecture is claimed.

## v0.2 navigation and selection

The subsequent v0.2 slice is documented in [V0_2_NAVIGATION_REVIEW.md](V0_2_NAVIGATION_REVIEW.md), with current logs under `docs/validation/v0.2-navigation`. Native ARM64 macOS, Windows/MSVC and Debian builds pass the GUI suite. Windows and Debian broker/final-track regressions also pass (two expected POSIX-only broker skips on Windows). This is application/build validation, not a repeat of the complete v0.1 relocatable-package qualification. Native macOS accessibility inspection required the recorded upstream Qt Cocoa backport.

## v1.3 Disk Format Editor package checks

The published v1.2 macOS ARM64 DMG is the compatibility baseline. Its SHA-256 matched the release manifest and GitHub asset digest. The native helper accepted both example definitions, and the broker opened and exported the same 128-byte IMD payload using the standard sidecar and both legacy variants. [v1.2 checksum manifest](https://github.com/CPMArchives/cpmimage/releases/download/v1.2/SHA256SUMS.txt), [Disk Format Editor spec](V1_3_DISK_FORMAT_EDITOR.md)

```text
191eb538e83f55946a3f5775310e4747aadd76fefed1baf51f16922f80ae8e13
```

Run the common package checker against the assembled application's executable. It resolves the private interpreter and native helper through the launcher diagnostics. It clears executable and library search overrides, poisons the inherited Python environment, and uses a temporary definition store. The checker validates both bundled Draft 2020-12 schemas and verifies that `jsonschema` and the platform's native `rpds` extension import from the private runtime. It also checks existing export oracles, overwrite refusal, 20 repeated exports, and the final-track cases when a manifest is supplied.

```sh
python3 packaging/scripts/check-package.py /path/to/assembled/executable \
  --fixture fixtures/ibm3740-independent.raw \
  --final-track-manifest fixtures/final-track-manifest.json \
  --report packaging/results/v1.3-package.json
```

The editor checks import, render, validate, and save both example definitions through the packaged broker. The standard IMD mapping and both legacy sidecars open and export the exact marker from physical sector ID 2. All three IMD inputs and sidecars are compared byte-for-byte. Both legacy forms are rejected for strict writes and remain readable through the legacy path.

| Negative case | Rejecting layer | Inputs checked after rejection |
|---|---|---|
| `seclen 384` with `blocksize 1024` | Native helper, `invalid_definition` | `diskdefs` |
| IMD inventory differs on track `0/1` | Broker mapping verifier, `physical_mapping` | `diskdefs`, IMD, sidecar |
| Malformed mapping JSON | Broker JSON decoder, `physical_mapping` | `diskdefs`, IMD, sidecar |

The checker requires all listed inputs and the package contents to remain byte-for-byte intact. Its report records each rejection layer separately.

### Local v1.3 candidate results

| Target | Packaged regression | Qt checks |
|---|---|---|
| macOS ARM64 | Passed from the read-only DMG at a path containing spaces and Unicode. Code signatures verified and 70 Mach-O files audited. | 38 Qt checks passed with the bundled Python and helper, including the historical SIG/M fixture. |
| Linux ARM64 | Passed in the Debian 13 ARM64 guest. | Both focused editor tests passed with the bundled Python and helper. |
| Linux x86-64 | Passed in the Debian 13 x86-64 guest. | Both focused editor tests passed with the bundled Python and helper. |
| Windows ARM64 | Passed using the extracted payload of the Windows 11 ARM64 single-file candidate. | Both focused editor tests passed with the bundled Python and helper. |
| Windows x86-64 | Passed using the extracted payload of the Windows 11 x86-64 single-file candidate and its native Windows Qt plugin. | Built the GUI test target. Packaged runtime checks ran through the native broker and helper. |

The macOS assembly used the v1.3 packaging script. The Linux and Windows guest runs rebuilt the application and restaged the editor and pinned wheels into the existing target runtime bundles. The candidates execute with `jsonschema` 4.26.0, `attrs` 26.1.0, `referencing` 0.37.0, `jsonschema-specifications` 2025.9.1, and the target-specific `rpds-py` 2026.6.3 wheel. Each verified target passed both legacy-sidecar cases and all three negative cases. These checks qualify the local candidates for review. Release publication remains pending.
