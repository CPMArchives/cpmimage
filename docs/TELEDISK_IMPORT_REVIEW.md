# TeleDisk import investigation

The v1.2 packaged macOS ARM64 LibDsk 1.4.2 dylib registers the `tele` driver. A scratch probe linked directly against that dylib opened all seven historical samples below. Uppercase `TD` uses the ordinary driver. Lowercase `td` opened with LibDsk's existing `tdlzh` decompressor. Neither a successful open nor the geometry probe establishes that all sectors can be read correctly.

The archive tree is at `~/projects/cpm-images/maslin_archives`. The existing test-image collection is at `~/projects/cpm-images/cpmimage-test-images`. The probed copies matched their archive members byte for byte. The probes did not write to archival originals.

| Archive member | Signature | SHA-256 | Packaged reader result |
| --- | --- | --- | --- |
| `1995/sydex/osborne/os1syss.td0` | td | `dabd0883291d558c07acd2284e76320901ba48b5f3c1cd448c740abe5db81855` | Geometry and first-track IDs failed with default/probed geometry |
| `1995/sydex/tandy/monte-22.td0` | td | `bbd6105e495f4ca50c28ce1b1521f06970d47a71ff4bbbfd26f26952f64f67b7` | Geometry probe failed; 18 first-track IDs returned |
| `1995/sydex/ampro/lb-cpm-g.td0` | td | `ebe7f4e1c256921401899a72805fd4fce73e57b9bfca2906afa9fa9bc9974340` | 40 cylinders, two heads, ten 512-byte sectors per head, IDs 17–26 in interleaved encounter order |
| `1995/sydex/xerox/820sys8s.td0` | TD | `b92198151378a34bab2a2b6cfcbcde800606c588a1c15ed0ec0636587b01b086` | Geometry and first-track IDs failed with default/probed geometry |
| `1995/sydex/altos/a8-2cpm2.td0` | td | `9bc4507cd19294a20dabdd010ec8f3c3433336e18656bc36ba12ee32ee17953a` | Geometry and first-track IDs failed with default/probed geometry |
| `1995/sydex/8in/altair1.td0` | TD | `8991cedb0311c64b74f7947efe200f5dd521790403506a61d1809d5f0398bbb9` | Geometry and first-track IDs failed with default/probed geometry |
| `1995/sydex/bondwell/bondwl02.td0` | td | `0a27182ed6980e8c129da687f3de0bc2e7a9e07774e84dd721ffb4cef4aa71fa` | Geometry probe failed; 18 first-track IDs returned in interleaved encounter order |

## Why import needs a qualification step

- A synthetic ordinary TD0 sector marked `0x10` contained no payload. The packaged `dsk_xread` returned success and supplied 128 bytes of `0xF6`. The pinned `drvtele.c` explains this result. It fills absent sectors in `tele_readsec`, and `tele_xread` only reports `0x20` as missing data. The latter also resets its read status after copying data. A generic mapped sector transfer can therefore accept invented bytes.
- LibDsk's TeleDisk driver has no native track-inventory implementation in the pinned build. The public `dsk_ptrackids` fallback returned useful IDs for some geometries. It does not expose sector syndrome flags. `dsk_copy` is absent from the packaged library.
- Existing structured-image code verifies a physical mapping before passing bytes to cpmtools. An Ampro sample has two sides and IDs 17–26 in interleaved order. Neither the encounter order nor a successful content listing establishes the correct RAW track-side and sector-slot order.
- Scratch conversions and independent extraction references already exist for several corpus images. They are useful as comparison inputs for a future implementation. This probe did not qualify an application import, Save/Discard behavior, or the resulting filesystem contents.

## Recommended design for review

Keep LibDsk 1.4.2 as the decoder and add a narrowly scoped read-only helper operation backed by a small, separately tracked LibDsk patch. Expose each physical track's full sector inventory with ID, actual size and status from the existing decoded TeleDisk stream. Propagate read errors and explicitly reject missing data before constructing RAW. Qualify both ordinary and advanced compression against the packaged build. This reuses the pinned parser for compressed inputs and avoids depending on its lossy public sector-ID fallback.

Feed the verified inventory into the existing mapping machinery. Start with unambiguous single-sided layouts. Use a verified physical profile when side or sector-slot order is ambiguous. Reject duplicate IDs, absent or damaged sectors, incomplete tracks, and layouts that cannot be represented by the selected RAW definition. Keep CP/M filesystem translation in cpmtools.

Reuse Open's format selection and the existing source-less working session lifecycle. Carry a proposed basename through the broker result to the tab and the initial Save As field. Exercise the unpublished session's dirty state, Cancel, Discard, Save, reopen, and normal RAW edits. Require an independent file-byte comparison before accepting each representative mapping.

An application-side metadata scanner could avoid patching the library for ordinary TD0, but it would need access to the decompressed record stream for lowercase TD0. A dependency upgrade could expose better internals, but would require broader compatibility qualification. Neither is necessary if the narrow pinned patch proves sufficient.

## LibDsk 1.5.22 scratch qualification

The macOS ARM64 scratch build used the downloaded 1.5.22 source plus a one-line driver change. TeleDisk syndrome bits `0x10` and `0x20` both mark a sector with no payload in the LDBS status. The synthetic `0x10` read now returns `No data`. The output buffer still contains filler, so the caller must discard it on error. The existing IMD inventory validator also rejects the missing sector after a private LibDsk `dsk_copy` to IMD.

The scratch path decoded TD0 to private IMD with 1.5.22 and used the existing mapped reader to reconstruct RAW. The existing broker opened each reconstructed RAW using its reference definition. The RAW files matched earlier reference conversions byte for byte. Each source TD0 hash stayed unchanged. The Ampro mapping used alternating heads per cylinder and ascending IDs 17–26, verified against the existing RAW reference. This test does not establish automatic discovery of that mapping.

| Maslin image | RAW bytes | Directory entries | Extracted file | File comparison |
| --- | ---: | ---: | --- | --- |
| Osborne 1 `os1syss` | 102,400 | 9 | PIP.COM, 7,424 bytes | Matches standalone cpmcp |
| Montezuma Micro `monte-22` | 184,320 | 25 | ASM.COM, 8,192 bytes | Matches standalone cpmcp |
| Ampro `lb-cpm-g` | 409,600 | 63 | ASM.COM, 8,192 bytes | Matches standalone cpmcp |
| Xerox 820 `820sys8s` | 256,256 | 13 | STAT.COM, 5,248 bytes | Matches the existing packaged extraction |

The unmodified cpmtools 2.23 oracle did not list the Xerox RAW correctly using the reference definition. The packaged cpmtools utility did extract STAT.COM, with SHA-256 `614d0b1d66466177e5b2bf585251d53d1e24eda34ada136b4ae178ab5944dc73`. Xerox has a byte-exact RAW reference comparison and a packaged extraction comparison. Independent extraction remains open.

Mapping checks (5), broker checks (13), catalogue checks (6), and final-track fixtures (6) passed. JV3 and DMK publication passed. IMD publication failed only with 1.5.22. The same test suite passed with the packaged 1.4.2 helper. A direct reproduction requested IMD mode 5 and the 1.5.22 writer produced mode 4. The 1.5.22 LDBS representation merges the 250/300 kbps distinction, and the IMD writer chooses between them using sector count. CP/M Image rejected the output before publication because its recording mode differed from the profile.

Keep 1.4.2 packaged while evaluating a TD0-only backend extension or qualifying an upgrade that preserves existing IMD writer behavior. No production TD0 GUI import or dependency change was made.

## LibDsk 1.4.2 TD0-only prototype

A scratch patch to `drvtele.c` exposed the sequential, decompressed sector records through the existing TeleDisk decoder. The prototype rejected absent payloads, damaged/deleted status flags, duplicate tracks or sector IDs, inconsistent address IDs, and truncated or invalid sector encodings. Its stricter public read path returns `No data` before copying filler and propagates sector decode errors. A scratch wrapper removed the partial private output on failure. The patch left the IMD, JV3, and DSK drivers unchanged.

The four historical samples above produced identical RAW bytes through this direct TD0 path, without an IMD intermediate. Explicit maps supplied RAW sector slots and the Ampro head order. The broker extracted the listed files from all four RAW images, and each matched unmodified cpmtools 2.23 built with its POSIX RAW backend. A synthetic absent payload, duplicate sector ID, duplicate track, and truncated payload were rejected.

The Xerox discrepancy was reproduced with the LibDsk-enabled cpmtools 2.23 oracle. Its geometry fallback interpreted 77 logical tracks as 38 cylinders by two heads. Changing only the odd-track branch in the scratch oracle gave 77 cylinders by one head and restored extraction. STAT.COM then matched the unmodified POSIX cpmtools build, the packaged extraction, and the broker byte for byte. The problem was in the oracle's device geometry, not the reconstructed RAW.

The existing container publication suite passed all seven tests on macOS ARM64, including the IMD, JV3 and DMK Save As subcases. The IMD mode mismatch seen with 1.5.22 did not recur.

The prototype did not validate every TeleDisk CRC or qualify additional layout variants. The next integration step would be a broker-private conversion with bounded inventory-to-RAW mapping and the existing unsaved-tab lifecycle. GUI integration and package changes are pending review.

## Automatic import and validation

The macOS ARM64 review build uses pinned LibDsk 1.4.2 with a TD0-only patch. The reader checks header, comment, track and decoded sector CRCs, along with the track and sector inventory. It rejects damaged or missing data, inconsistent address IDs, duplicates and unsupported encodings. A loaded comment does not mark the archive modified.

The broker derives RAW slots for uniform single-sided layouts with contiguous cylinders and consistent zero- or one-based sector IDs. Multi-sided and noncanonical layouts require a matching `.mapping.json` checked against the complete inventory. Definitions with byte offsets or boot-derived geometry are outside this import path. cpmtools still applies filesystem skew.

Opening TD0 creates a dirty private RAW session using the original basename and a `.raw` extension. The first Save offers that name and publishes through the normal RAW workflow. No intermediate output or TD0 write is required. The four Maslin cases above passed automatic broker import, Save, reopen and extracted-file SHA-256 comparison. The Ampro double-sided sample also verified rejection without a profile. Synthetic fixtures verified rejection of bad track and sector CRCs, missing data and duplicate sector IDs. The existing container Save As suite, including IMD, passed. The focused Qt test verified the derived unsaved Xerox tab name, proposed Save filename and transition to a saved tab.

## Cross-platform review checks

Windows 11 ARM64 and Debian 13.7 ARM64 passed the focused broker and Qt checks with rebuilt native helpers. On each platform, all four Maslin samples matched the reference RAW and extracted-file SHA-256 hashes after Save and reopen. The tests also verified a private rename and unchanged source bytes. Synthetic CRC, missing-sector and duplicate-ID checks passed. The Qt test verified the Xerox unsaved-tab indicator, proposed `XEROX.raw` filename and successful first Save.

The initial Windows run exposed LibDsk's POSIX `/tmp` fallback for advanced compression in the cross-built helper. The TD0 helper subprocess now points `TMPDIR` at its own private working directory. All three advanced-compression samples passed on rerun. The Windows and Debian GUI checks used Qt's offscreen platform plugin. No x86-64 or release-package qualification was performed.
