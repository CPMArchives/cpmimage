# CP/M Image v1.3 Disk Format Editor

## Scope

Create, import, duplicate, validate, save, and export standalone cpmtools `diskdefs` definitions. Saved definitions appear in the format picker for opening and creating images. Checking a definition against an image preserves the source image and sidecar bytes.

The compatibility baseline is the published v1.2 macOS ARM64 package, with patched cpmtools 2.23 and LibDsk 1.4.2. The definition record and strict mapping sidecar write schema specify v1.3 behavior. Existing sidecars retain the v1.2 filename, reader, and verification rules. [v1.2 release](https://github.com/CPMArchives/cpmimage/releases/tag/v1.2)

## Editor fields and defaults

| Section | Field | New definition | Behavior |
|---|---|---|---|
| Identity | Format identifier | Blank, required | Becomes the unique `diskdef` name in the active catalogue. |
| Identity | Display name | Blank, optional | The picker falls back to the identifier. |
| Sector layout | Bytes per sector (`seclen`) | Blank, required | Sector size in the cpmtools view. |
| Sector layout | Track count (`tracks`) | Blank, required | Count of cpmtools track groups. RAW groups may be synthetic. |
| Sector layout | Sectors per track (`sectrk`) | Blank, required | Width of the CP/M skew map. |
| Filesystem | Allocation block size (`blocksize`) | Blank, required | Bytes per allocation block. |
| Filesystem | Directory entries (`maxdir`) | Blank, required | Number of 32-byte directory entries. |
| Filesystem | Reserved directory blocks (`dirblks`) | Automatic | Show the minimum. A larger override reserves blocks without adding entries. |
| Filesystem | OS (`os`) | Default (CP/M 2.2) | Default, CP/M 2.2, CP/M 3, ISX, P2DOS, or Z-System. Default omits the directive. Explicit CP/M 2.2 emits `os 2.2`. |
| Boot reservation | Unit and count | Tracks, 0 | Choose tracks or sectors. Emit `boottrk` or `bootsec`. Show effective sectors and bytes. |
| Advanced | Sector ordering | Default (sequential) | Default, Skew step, or Explicit table. The table maps logical indices to zero-based RAW slots. Initialize it from the current permutation when track width is known. |
| Advanced | Image offset (`offset`) | 0 bytes | Choose bytes, KiB, MiB, sectors, or tracks. Relative values follow geometry edits. Show effective bytes. |
| Advanced | Logical extent factor (`logicalextents`) | Automatic | Show the calculated factor and permit an override. |
| Advanced | LibDsk standard geometry (`libdsk:format`) | None | Optional name for direct LibDsk use. Show applicable restrictions during an image check. |
| Image check | Image and mapping profile | None | Optional. Label geometry as observed, profile-supplied, or inferred. |

Provide **New**, **Duplicate**, **Import diskdefs**, **Check against image**, **Save definition**, and **Export diskdefs**. Bundled definitions can be inspected or duplicated under a new identifier. The built-in `amstrad` entry derives geometry from the boot sector and is unavailable for standalone editing, duplication, or initialization.

Show nominal RAW size, reserved sectors and bytes, filesystem blocks, effective directory reservation, initial file-data capacity, and effective extent factor when their inputs are complete. Display mounted free space independently. For a verified structured mapping, **Use mapped sector layout** fills only `seclen`, `tracks`, and `sectrk`. Allocation, directory, boot, OS, and CP/M skew remain authored settings. Show physical sector IDs independently from CP/M skew. Synthetic RAW definitions require no physical geometry or mapping profile.

## Definition validation and rendering

Drafts may be incomplete. An active saved definition or canonical export requires schema validation, editor checks, and a successful native helper `describe` call on the exact rendered block.

- Require a unique identifier of 1–160 printable ASCII characters excluding whitespace, `#`, and `;`. Reserve `amstrad` and reject trailing LF or CRLF. Retain the 256 KiB UTF-8 import limit and reject NUL. Limit emitted lines to 254 UTF-8 bytes before the newline.
- Render an explicit header and `end`, with fields in a fixed order. Use decimal integers, one boot directive, at most one skew directive, and lowercase `os`. Omit automatic directory and extent directives, default skew and OS, zero offset, and an absent LibDsk name.
- On import, interpret cpmtools numeric spellings. Accept `OS` as an alias. Treat `dirblks 0` and `logicalextents 0` as Automatic. Preview `bootsec` when both boot directives occur, including zero. Preview `skewtab` when both skew directives occur. Flag duplicate scalar keys, redundant directives, multiple OS directives, unknown directives, and noncanonical offset spelling. Repeated OS directives can accumulate flags. Preserve the original block and require explicit resolution before canonicalization that would lose information or change behavior.
- Convert offsets to bytes using multipliers `1`, `1024`, `1048576`, `seclen`, or `sectrk × seclen`. Limit the effective value to `2,147,483,647`. Emit decimal bytes to avoid the native ordering dependency for `S` and `T` suffixes.

| Native check | Rule |
|---|---|
| Sector and block sizes | `seclen` is 128–16,384 and divisible by 128. `blocksize` is 128–16,384 and divisible by `seclen`. |
| Track capacity | `tracks > 0`, `1 ≤ sectrk ≤ 65,536`, and `N × seclen ≤ 2,147,483,647`, where `N = tracks × sectrk`. Nominal RAW size is `offset + N × seclen`. |
| Boot reservation | `R = bootsec` or `boottrk × sectrk`, according to the authored unit. Require `0 ≤ R < N`. Reserve `R × seclen` bytes. The logical directory begins at sector `R` before skew. |
| Allocation and directory | `B = floor((N − R) × seclen / blocksize)`, `D = ceil(maxdir × 32 / blocksize)`, and `1 ≤ maxdir ≤ 65,536`. Automatic `dirblks` resolves to `D`. Require `D ≤ effective dirblks ≤ B ≤ 65,536`. Initial file-data capacity is `(B − effective dirblks) × blocksize`. |
| Extent factor | Use `P = 16` one-byte pointers when `B ≤ 256`, otherwise eight two-byte pointers. Automatic is `max(1, floor(P × blocksize / 16384))`. An explicit factor is 1–32. Above 1, require `factor × 16384 ≤ P × blocksize`. |
| Skew step | `0 ≤ step ≤ 2,147,483,647 − sectrk`. Default is 1. Steps 0 and 1 yield sequential order. |
| Skew table | Exactly `sectrk` unique entries in `0..sectrk−1`, within both the 1,024-entry cap and rendered line limit. |
| Offset | The helper checks the rendered byte offset. The editor owns authored-unit conversion. |

For logical sector `L`, the RAW track index is `floor(L / sectrk)` and the RAW slot is the effective skew entry at `L % sectrk`. The byte address is `offset + (track × sectrk + slot) × seclen`. A structured mapping selects the physical track and sector ID that supply that RAW slot. [Parser patch](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/packaging/source/cpmtools-2.23-diskdefs.patch), [Helper](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/experiments/process-comparison/helper.c#L55)

## Image and operation validation

Show definition validity, image compatibility, and availability of the requested operation independently. A readable directory is evidence for a candidate format. Multiple definitions can fit one image.

| Path | Checks |
|---|---|
| GUI RAW | Check size and sector alignment relative to the authored offset, then native directory and allocated-sector access. An unused short tail may pass existing v1.2 checks. |
| GUI IMD, JV3, DMK | Use the sibling sidecar when present. Allow canonical inference when absent. Verify the mapping against the definition and image inventory, then check sector access. IMD and JV3 use mapped LibDsk transfers. DMK uses the existing track parser. |
| GUI standard CPCEMU DSK | Require 40 tracks, one side, 10 × 512-byte sectors, zero offset, ordered IDs 1–10, and supported headers and flags. Allow absent `libdsk:format`, `ampro200`, or `pcw200`. Reject Extended DSK on this path. |
| Direct cpmtools LibDsk | Verify driver access, sector-aligned offset, and any named standard geometry. A named geometry can override the direct-device geometry. |
| Create or initialize | Require a valid standalone definition and nominal size within 128 MiB. Existing-image initialization preserves capacity. Labels and timestamps require the supported CP/M Plus path. |
| Structured save | Verify destination mapping and container-specific write support, including any required DMK template. |

The broker decodes mapping JSON, checks that override keys name mapped tracks, and requires each merged track to supply `mode`, `size`, and `sectors`. Track count must equal `definition.tracks`, size and sector count must match `seclen` and `sectrk`, and effective offset must be zero. Inventory verification precedes sector transfer. The broker and operation backend own inventory, transfer, and filesystem-read failures. The native `describe` operation validates definition text alone. [Broker](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/broker.py), [Mapping verifier](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/container_mapping.py)

## Persistence and compatibility

Persist authored fields in the versioned definition record. Use `null` for Automatic or Default where specified. Keep `record_id` across edits and renames, and assign a new UUID on duplication. `import_source.diskdefs_text` preserves provenance. Canonical rendering consumes authored fields. Preserve records with unsupported schema versions.

Derive effective values from the rendered block. Its exact UTF-8 SHA-256 is the revision checked by the broker and remembered associations. Renaming changes the rendered revision. Pin the exact definition to each open working copy. Applying a change requires reopening against the selected revision.

Load existing `<image filename>.mapping.json` files using v1.2 reader, per-track merge rules, and broker checks. Extra metadata and incomplete defaults remain acceptable when the merged mapping and image pass. Strict-schema diagnostics during inspection or import do not block legacy open. Preserve source sidecar bytes on open, inspection, and export, including failures. A present invalid sidecar retains the v1.2 failure behavior. Canonical inference is the fallback for an absent sidecar.

Apply the strict write schema to new and explicitly rewritten sidecars. Preview supplied defaults and metadata removal before replacing a legacy file. Preserve the original if conversion remains unresolved. `track_order` assigns RAW track indices. Merged `sectors` assigns physical IDs to RAW slots. The sidecar carries addressing information only.

## JSON Schemas

Both schemas use Draft 2020-12. Enable format assertion for UUIDs. Cross-field and operation checks are specified above.

### Saved definition schema

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "CP/M Image saved disk definition",
  "type": "object",
  "additionalProperties": false,
  "required": [
    "schema_version",
    "record_id",
    "identifier",
    "definition"
  ],
  "properties": {
    "schema_version": {
      "const": 1,
      "type": "integer"
    },
    "record_id": {
      "type": "string",
      "format": "uuid"
    },
    "identifier": {
      "type": "string",
      "minLength": 1,
      "maxLength": 160,
      "allOf": [
        {
          "not": {
            "const": "amstrad"
          }
        },
        {
          "not": {
            "pattern": "[^\\x21-\\x22\\x24-\\x3A\\x3C-\\x7E]"
          }
        }
      ]
    },
    "display_name": {
      "type": "string",
      "minLength": 1
    },
    "definition": {
      "$ref": "#/$defs/definition"
    },
    "import_source": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "diskdefs_text"
      ],
      "properties": {
        "diskdefs_text": {
          "type": "string",
          "minLength": 1
        }
      }
    }
  },
  "$defs": {
    "definition": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "seclen",
        "tracks",
        "sectrk",
        "blocksize",
        "maxdir",
        "boot",
        "dirblks",
        "skew",
        "os",
        "offset",
        "logicalextents",
        "libdsk_format"
      ],
      "properties": {
        "seclen": {
          "type": "integer",
          "minimum": 128,
          "maximum": 16384,
          "multipleOf": 128
        },
        "tracks": {
          "type": "integer",
          "minimum": 1
        },
        "sectrk": {
          "type": "integer",
          "minimum": 1,
          "maximum": 65536
        },
        "blocksize": {
          "type": "integer",
          "minimum": 128,
          "maximum": 16384,
          "multipleOf": 128
        },
        "maxdir": {
          "type": "integer",
          "minimum": 1,
          "maximum": 65536
        },
        "boot": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "unit",
            "count"
          ],
          "properties": {
            "unit": {
              "enum": [
                "tracks",
                "sectors"
              ]
            },
            "count": {
              "type": "integer",
              "minimum": 0,
              "maximum": 2147483647
            }
          }
        },
        "dirblks": {
          "type": [
            "integer",
            "null"
          ],
          "minimum": 1,
          "maximum": 65536
        },
        "skew": {
          "$ref": "#/$defs/skew"
        },
        "os": {
          "enum": [
            null,
            "2.2",
            "3",
            "isx",
            "p2dos",
            "zsys"
          ]
        },
        "offset": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "value",
            "unit"
          ],
          "properties": {
            "value": {
              "type": "integer",
              "minimum": 0,
              "maximum": 2147483647
            },
            "unit": {
              "enum": [
                "bytes",
                "kib",
                "mib",
                "sectors",
                "tracks"
              ]
            }
          }
        },
        "logicalextents": {
          "type": [
            "integer",
            "null"
          ],
          "minimum": 1,
          "maximum": 32
        },
        "libdsk_format": {
          "anyOf": [
            {
              "type": "null"
            },
            {
              "type": "string",
              "minLength": 1,
              "not": {
                "pattern": "[\\s\\x00-\\x1F\\x7F#;]"
              }
            }
          ]
        }
      }
    },
    "skew": {
      "oneOf": [
        {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "mode"
          ],
          "properties": {
            "mode": {
              "const": "default"
            }
          }
        },
        {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "mode",
            "step"
          ],
          "properties": {
            "mode": {
              "const": "step"
            },
            "step": {
              "type": "integer",
              "minimum": 0,
              "maximum": 2147483646
            }
          }
        },
        {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "mode",
            "slots"
          ],
          "properties": {
            "mode": {
              "const": "table"
            },
            "slots": {
              "type": "array",
              "minItems": 1,
              "maxItems": 1024,
              "uniqueItems": true,
              "items": {
                "type": "integer",
                "minimum": 0,
                "maximum": 65535
              }
            }
          }
        }
      ]
    }
  }
}
```

### Strict mapping sidecar write schema

Apply this schema to v1.3 writes. The override-key pattern rejects trailing LF or CRLF.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "CP/M Image physical mapping sidecar",
  "type": "object",
  "additionalProperties": false,
  "required": [
    "track_order",
    "default"
  ],
  "properties": {
    "track_order": {
      "type": "array",
      "minItems": 1,
      "maxItems": 512,
      "uniqueItems": true,
      "items": {
        "type": "array",
        "minItems": 2,
        "prefixItems": [
          {
            "type": "integer",
            "minimum": 0,
            "maximum": 255
          },
          {
            "type": "integer",
            "minimum": 0,
            "maximum": 1
          }
        ],
        "items": false
      }
    },
    "default": {
      "$ref": "#/$defs/trackDefaults"
    },
    "tracks": {
      "type": "object",
      "maxProperties": 512,
      "propertyNames": {
        "pattern": "^(0|[1-9][0-9]?|1[0-9]{2}|2[0-4][0-9]|25[0-5])/[01](?![\\s\\S])"
      },
      "additionalProperties": {
        "$ref": "#/$defs/trackFields"
      }
    }
  },
  "$defs": {
    "trackFields": {
      "type": "object",
      "additionalProperties": false,
      "required": [],
      "properties": {
        "mode": {
          "type": "integer",
          "minimum": 0,
          "maximum": 5
        },
        "size": {
          "type": "integer",
          "minimum": 128,
          "maximum": 16384,
          "multipleOf": 128
        },
        "sectors": {
          "type": "array",
          "minItems": 1,
          "maxItems": 256,
          "uniqueItems": true,
          "items": {
            "type": "integer",
            "minimum": 0,
            "maximum": 255
          }
        }
      }
    },
    "trackDefaults": {
      "$ref": "#/$defs/trackFields",
      "required": [
        "mode",
        "size",
        "sectors"
      ]
    }
  }
}
```

## Examples

### Synthetic RAW

64 synthetic track groups of 32 × 128-byte sectors give 256 KiB nominal capacity, 256 allocation blocks, four directory blocks, and 252 initial file-data blocks. No sidecar is needed.

```json
{
  "schema_version": 1,
  "record_id": "df2e69c1-6854-46d6-a420-68d2dd549aa0",
  "identifier": "synthetic-256k",
  "definition": {
    "seclen": 128,
    "tracks": 64,
    "sectrk": 32,
    "blocksize": 1024,
    "maxdir": 128,
    "boot": {"unit": "tracks", "count": 0},
    "dirblks": null,
    "skew": {"mode": "default"},
    "os": null,
    "offset": {"value": 0, "unit": "bytes"},
    "logicalextents": null,
    "libdsk_format": null
  }
}
```

```text
diskdef synthetic-256k
  seclen 128
  tracks 64
  sectrk 32
  blocksize 1024
  maxdir 128
  boottrk 0
end
```

### IMD with an explicit mapping

Four tracks across two cylinders and two heads give 8 KiB nominal RAW capacity. Four sectors are reserved for boot, and one of the six filesystem blocks is reserved for the directory.

```json
{
  "schema_version": 1,
  "record_id": "6366d542-5ed2-40df-b84e-ce6340e676ce",
  "identifier": "example-imd-8k",
  "definition": {
    "seclen": 512,
    "tracks": 4,
    "sectrk": 4,
    "blocksize": 1024,
    "maxdir": 32,
    "boot": {"unit": "sectors", "count": 4},
    "dirblks": null,
    "skew": {"mode": "default"},
    "os": "2.2",
    "offset": {"value": 0, "unit": "bytes"},
    "logicalextents": null,
    "libdsk_format": null
  }
}
```

```text
diskdef example-imd-8k
  seclen 512
  tracks 4
  sectrk 4
  blocksize 1024
  maxdir 32
  bootsec 4
  os 2.2
end
```

**`example.imd.mapping.json`**

```json
{
  "track_order": [[0, 0], [0, 1], [1, 0], [1, 1]],
  "default": {
    "mode": 5,
    "size": 512,
    "sectors": [1, 3, 2, 4]
  }
}
```

The profile assigns sector ID `3` to RAW slot `1` and ID `2` to slot `2`. Default CP/M skew preserves those slot indices.

## Verification and acceptance

The SHA-256 of `CPMImage-macOS-arm64.dmg` matched both the publisher's manifest and GitHub's asset digest. The tests used the native helper, broker, and Python runtime bundled in the verified DMG. [Published package](https://github.com/CPMArchives/cpmimage/releases/download/v1.2/CPMImage-macOS-arm64.dmg), [Checksum manifest](https://github.com/CPMArchives/cpmimage/releases/download/v1.2/SHA256SUMS.txt)

```text
191eb538e83f55946a3f5775310e4747aadd76fefed1baf51f16922f80ae8e13
```

| Published-package case | Observed result |
|---|---|
| Rendered `synthetic-256k` | Helper accepted the definition and reported the specified geometry, zero boot sectors, four directory blocks, and extent factor 1. |
| Rendered `example-imd-8k` | Helper accepted the definition and reported the specified geometry, four boot sectors, one directory block, and extent factor 1. |
| Standard sidecar | Broker listed `PROBE.BIN` and exported the exact 128-byte marker from physical sector ID 2. The fixture stored IDs in ascending order, so the export verified the sidecar's nonsequential slot order. IMD and sidecar were byte-for-byte intact. |
| Existing sidecar with extra top-level metadata | Broker opened the IMD and exported the same bytes. IMD and sidecar, including metadata and formatting, were byte-for-byte intact. |
| Existing sidecar with empty defaults and complete overrides for all four tracks | Broker opened the IMD and exported the same bytes. IMD and sidecar, including the empty default and complete overrides, were byte-for-byte intact. |

For the standard sidecar, the published package and `d42b0ed8` source build matched on helper descriptions, catalogue data, listing, free space, and exported bytes. The comparison excluded temporary paths, session IDs, timing, and process counts. Both legacy variants matched the standard export and are v1.3 regression cases.

A valid IMD and its matching sidecar opened as the control for the broker negative tests. The files listed below were compared before and after each rejection.

| Negative case | Rejecting layer | Observed result | Files byte-for-byte intact |
|---|---|---|---|
| `seclen 384` with `blocksize 1024` | Native helper's `diskdefs` parser | `describe` exited with status 1 and reported `invalid disk definition near line 9`. Both values meet their independent schema bounds. | `diskdefs` |
| Sidecar and IMD inventory disagree | Broker mapping verifier | Track `0/1` contained ID 5 in place of ID 4. Returned `physical_mapping` with `Container sectors differ on track 0/1.` | `diskdefs`, `example.imd`, `example.imd.mapping.json` |
| Sidecar ends after `{"track_order": [` | Broker JSON decoder | Returned `physical_mapping` with `Expecting value: line 1 column 18 (char 17)`. | `diskdefs`, `example.imd`, `example.imd.mapping.json` |

In v1.3, keep JSON decoding and inventory diagnostics distinguishable even when they share an error code. Preserve failing inputs for correction.

Both schemas passed the Draft 2020-12 metaschema check with Python `jsonschema` 4.26.0 and format checking enabled. The RAW and IMD records passed. Identifier and override-key LF/CRLF cases failed as specified. The IMD, v1.2 77-track conversion/initialization profile, and v1.2 two-track partial-override profile passed the strict schema and v1.2 mapping verifier. Extra metadata and empty defaults failed the strict write schema and passed legacy loading. A well-formed unmapped override and a wrong sector count passed the schema and failed mapping verification. [Mapping fixtures](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/tests/container_mapping_check.py), [Conversion fixtures](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/tests/container_save_check.py), [Initialization fixtures](https://github.com/CPMArchives/cpmimage/blob/d42b0ed8/prototype/tests/initialize_check.py)
