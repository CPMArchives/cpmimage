# CP/M Image v1.3 Disk Format Editor implementation plan

The [full spec](V1_3_DISK_FORMAT_EDITOR.md) defines the contract. Use `d42b0ed8` as the source baseline and the checksum-verified published v1.2 macOS ARM64 package as the behavioral baseline. Implement validation first, then persistence and broker operations, Qt integration, and package verification.

## 1. Schemas, model, renderer, and import

| File | Change |
|---|---|
| `prototype/schemas/disk-format-definition.schema.json` (new) | Add the saved-definition Draft 2020-12 schema, including the full-string identifier rule and UUID format. |
| `prototype/schemas/mapping-sidecar.schema.json` (new) | Add the strict write schema with the full-string override key check. |
| `prototype/disk_format.py` (new) | Validate authored records, render canonical `diskdefs`, resolve offsets, enforce the UTF-8 line limit, compute the exact rendered digest, and invoke native `describe`. |
| `prototype/catalogue.py` | Add standalone import diagnostics and preserve source text for review. Reuse catalogue indexing and limits. Preserve bundled catalogue and FDF loading behavior. |
| `prototype/requirements-disk-format.txt` (new) | Pin the Draft 2020-12 validator used by the model. Include the required runtime dependencies during release staging. |
| `prototype/tests/disk_format_check.py` (new) | Test both schemas, UUID assertion, LF/CRLF rejection, byte-exact RAW and IMD rendering, omitted defaults, offset conversion, import diagnostics, deterministic digests, and native descriptions. |

## 2. Persistence and broker operations

| File | Planned change |
|---|---|
| `prototype/disk_format_store.py` (new) | Atomically save validated records in a writable user store outside the app bundle. Preserve stable record IDs through edits and renames, assign a new UUID on duplication, preserve unsupported versions, and check identifier collisions before activation. |
| `prototype/broker.py` | Add editor record loading, draft validation, image checks, saving, and canonical export. Reuse catalogue, native filesystem, and container operations. Use temporary storage for draft checks. Merge active records into open and new-image selection, pin exact definitions to sessions, and enforce revision checks on later opens. |
| `prototype/container_mapping.py` | Retain the v1.2 profile expansion and inventory checks for legacy reads. Add strict write validation and a legacy conversion preview. Preserve separate diagnostics for JSON decoding, definition-relative mapping, and observed inventory. |
| `experiments/process-comparison/helper.c` and `packaging/source/cpmtools-2.23-diskdefs.patch` | Reuse existing native `describe` and filesystem operations. They already provide the effective values and reject the indivisible-block case. |

Land legacy acceptance tests before changing the sidecar write path. A present malformed sidecar must fail open. Canonical inference applies when the sidecar is absent.

## 3. Qt editor and format picker

| File | Planned change |
|---|---|
| `prototype/src/disk_format_editor.h` and `prototype/src/disk_format_editor.cpp` (new) | Add the editor, fields and defaults, calculations, import diagnostics, and image check panel. Display definition validity, image compatibility, and operation availability independently. Enable **Use mapped sector layout** only after verification and limit it to the three geometry fields. Preview legacy sidecar conversion before explicit replacement. |
| `prototype/src/window.h` and `prototype/src/window.cpp` | Add editor entry points, use the existing broker request lifecycle, and refresh the picker after save. Preserve remembered image hash, format ID, definition hash, and container checks. Reopen to apply a changed definition. |
| `prototype/CMakeLists.txt` | Register editor sources in application and GUI test targets and follow existing test conventions. |
| `prototype/tests/gui_check.cpp` | Cover defaults, duplicate and import actions, field errors, calculations, picker refresh, session revision pinning, mapped geometry updates, and byte preservation when cancelling legacy conversion. |

## 4. Regression tests

Use a four-track IMD with physical IDs in ascending order and sidecar slots `[1, 3, 2, 4]`. The positive control must list `PROBE.BIN` and export the exact 128-byte marker from physical sector ID 2. Compare input bytes before and after each operation.

| File | Case | Required assertion |
|---|---|---|
| `prototype/tests/container_mapping_check.py` | Legacy profile expansion | Extra metadata is tolerated. Empty defaults work with complete overrides for all mapped tracks. The expected slot order survives merging. Strict writes reject the original legacy forms. |
| `prototype/tests/container_mapping_check.py` | Definition-relative mismatch | A well-formed unmapped override and a sector count different from `sectrk` pass structural validation and fail mapping verification. |
| `prototype/tests/container_save_check.py` | Existing sidecar with extra metadata | Broker opens and exports the exact marker. The IMD and sidecar, including metadata and formatting, stay byte-for-byte intact. |
| `prototype/tests/container_save_check.py` | Existing sidecar with empty defaults and four complete overrides | Broker opens and exports the same marker. IMD and sidecar stay byte-for-byte intact. |
| `prototype/tests/diskdefs_check.py` | `seclen 384` with `blocksize 1024` | Schema accepts the numeric fields. Native `describe` exits with status 1. Assert the diagnostic category without relying on its line number. `diskdefs` stays byte-for-byte intact. |
| `prototype/tests/broker_check.py` | Mismatched IMD inventory | Replace ID 4 with 5 on track `0/1` and retain the valid sidecar. Require `physical_mapping` and an inventory diagnostic identifying `0/1`. `diskdefs`, IMD, and sidecar stay byte-for-byte intact. |
| `prototype/tests/broker_check.py` | Malformed sidecar JSON | Truncate the sidecar after `{"track_order": [`. Require `physical_mapping` and a JSON decoder diagnostic. Canonical inference does not run. `diskdefs`, IMD, and sidecar stay byte-for-byte intact. |
| `prototype/tests/catalogue_check.py` and `prototype/tests/initialize_check.py` | Existing catalogue and initialization behavior | Bundled and external catalogue selection, FDF loading, remembered revisions, and creation with existing and editor-authored definitions pass. |

Run schema and renderer checks, direct native validation, mapping verifier checks, broker open and export tests, Qt tests, and packaged smoke tests in that order. Exercise the broker against the source build and packaged runtime. Repeat the published v1.2 fixtures against the v1.3 package before release.

## 5. Stage and document the package

| File | Planned change |
|---|---|
| `packaging/scripts/stage-app-sources.py` and `packaging/scripts/package-{macos,linux,windows}.py` | Verify that staging includes the schemas, editor modules, and validator dependencies on all platforms. Update staging as needed. |
| `packaging/scripts/check-package.py` | Check staged schemas and modules. Exercise the packaged broker with the positive control, both legacy profiles, and all three negative fixtures. |
| `docs/HELP.md` | Document definition creation, import, checking, and export, including authored versus observed geometry and explicit legacy sidecar conversion. |
| `docs/VALIDATION.md` | Record the published v1.2 DMG checksum and observed baselines, then add v1.3 packaged results, including failure layers and byte-preservation assertions. |

[Window and picker](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/src/window.cpp), [Catalogue loader](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/catalogue.py), [Broker](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/broker.py), [Mapping verifier](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/container_mapping.py)
