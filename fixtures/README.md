# Synthetic fixture corpus

All committed image files are generated test data, not operating-system distributions or copies of SIG/M/BetterCP/M disks. New fixture-generation code and data are GPL-3.0-or-later. `diskdefs` retains the pinned cpmtools definitions and explicit experimental layout additions; it is not a promise of support for every listed format.

- `ibm3740-independent.raw`: independently constructed CP/M 2.2 entries in user areas 0, 7 and 15, known binary payloads, record padding and patterned boot tracks.
- `ibm3740-empty.raw`: cleared directory, illustrating detection ambiguity.
- `ibm3740-truncated.raw`: missing required directory sectors; must be rejected.
- `ampro200-independent.dsk`: independent Ampro layout in a standard CPCEMU DSK container.
- `final-track-manifest.json` and its six images: files on the last track of all five supported raw layouts, plus a valid short IBM3740 image. Includes the last physical sector, two-byte allocation pointers and payload SHA-256 oracles.

Baseline fixtures can be regenerated into a temporary output directory without cpmtools:

```sh
python3 prototype/tests/make_independent_fixtures.py --output /tmp/cpmimage-fixtures
```

`prototype/tests/make_final_track_fixtures.py` regenerates the final-track set in this directory. Record/hash manifests identify the exact bytes used by the tests. Do not regenerate fixtures casually during a validation run.

The real SIG/M corpus was used for read-only local qualification only. The committed evidence contains filenames and hashes, not those image files. External inputs are not required to run the synthetic regression suites.
