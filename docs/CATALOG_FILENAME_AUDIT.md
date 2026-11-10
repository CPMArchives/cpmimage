# Complete SIG/M catalogue filename audit

Scanned the complete [`-CATALOG.ALL`](https://github.com/CPMArchives/SIGM/blob/main/-CATALOG.ALL) on 2026-11-10: **6,681 filename entries, 5,485 distinct filenames, all 310 volumes, 11,951 lines**. Supplementary catalogue/utility file rows are included, not only numbered program entries. No candidate file row was left unparsed.

Git blob: `8dc5b0ca05353f080e94fe3ecccf9bbb0f5d3834`. SHA-256: `adc4e2c729239e879e5e8ec37def8f67f685a6419e65b6213731a55c697c0f05`.

## Characters actually present

The extension separator `.` and fixed-column padding are structural and are not counted as embedded filename characters.

| Character | Catalogue entries | macOS/Linux filename component | Modern Windows long filename | Classic DOS 8.3 |
|---|---:|---|---|---|
| `/` | 278 | Illegal | Illegal | Illegal |
| `+` | 20 | Legal | Legal | Illegal |
| `$` | 7 | Legal | Legal | Legal |
| `&` | 3 | Legal | Legal | Legal |
| `!` | 1 | Legal | Legal | Legal |
| `@` | 4 | Legal | Legal | Legal |
| `-` | 735 | Legal | Legal | Legal |

**The complete set of embedded characters in this catalogue requiring translation is `/` for the three modern target OSes, and `/` plus `+` when classic DOS is included.** Slash occurs in 278 entries (61 distinct names); plus occurs in 20 entries. `$` occurs in seven entries and is legal on all four targets. Shell quoting is separate from filesystem legality: `$`, `&`, `!`, leading `-`, and other punctuation can need quoting or an option terminator. The GUI should not route filenames through a shell.

No DOS/Windows reserved device basename (CON, PRN, AUX, NUL, COM1–9, LPT1–9) was found. This audit concerns the catalogue, not every byte-level filename in every disk image. The catalogue itself may use normalized names.

## Every distinct slash name

Each name below appears in at least one catalogue entry. Full volume/line occurrences, including repetitions, are in [the machine-readable report](catalog-audit/2026-11-10.json).

- `/.ASM`
- `/.COM`
- `ADBUG/1.DBF`
- `ADD/REC.CMD`
- `ARTIL/M.PAS`
- `ARTIL/Z.COM`
- `ARTIL/Z.PAS`
- `BMAP7/11.ASM`
- `CP/M-NET.MSG`
- `CRCK10/6.ASM`
- `DB2SIG/M.LBR`
- `DIRLST/1.ASM`
- `DIRS10/1.ASM`
- `DU-10/26.ASM`
- `DU-8/12.ASM`
- `ED/ASM.COM`
- `ED/ASM.DOC`
- `ED/H19.COM`
- `FIND3/18.ASM`
- `FLIP-8/8.ASM`
- `FLUF11/T.LBR`
- `FMAP6/12.ASM`
- `FORM/LTR.CMD`
- `FORM/PRT.CMD`
- `I/O-CAP.ADD`
- `I/O-CAP.ASM`
- `I/OCAP11.ASM`
- `ISIS/CPM.ASM`
- `ISIS/CPM.DOC`
- `KID/KIP.DOC`
- `LBR/CARD.210`
- `LBR/CARD.211`
- `LIST/PRT.CMD`
- `MAIL/LAB.CMD`
- `MDIR8/17.ASM`
- `MOVE6/12.ASM`
- `RBBS-I/O.NQW`
- `REZ7/31.ASM`
- `REZ7/31.DOC`
- `SAMPLE/C.BAS`
- `SAMPLE/C.C`
- `SAMPLE/C.COM`
- `SAMPLE/M.PAS`
- `SAMPLE/N.BAS`
- `SAMPLE/S.BAS`
- `SAMPLE/S.COM`
- `SAMPLE/Z.COM`
- `SAMPLE/Z.PAS`
- `SD-12/15.ASM`
- `SD-2/6.ASM`
- `SFA/SAV.0`
- `SIG/M.CMD`
- `SIG/M.LIB`
- `SIG/M.SUB`
- `SIMP12/T.LBR`
- `SQ/USQ15.DOC`
- `TFX12/18.ASM`
- `USER-8/8.ASM`
- `WHICH/1.ASM`
- `WORM8/8.ASM`
- `XDIR6/28.ASM`

## Every distinct plus name

- `BIOS3+.DQC`
- `CP-M+.LBR`
- `CPM2-+.LBR`
- `CUB++.DAT`
- `DISPLAY+.ASM`
- `DISPLAY+.COM`
- `FIND+.ASM`
- `FIND+.COM`
- `FIND+2.LBR`
- `KPHANDY+.LBR`
- `M7AC+3.AQM`
- `M7BBS+.AQM`
- `MDM709C+.AQM`
- `MXO-R+10.AQM`
- `RDIR15+.LBR`
- `TYP+.ASM`
- `TYP+.COM`
- `UNERA+.LBR`
- `XSTAM+.ASM`
- `XSTAM+.COM`

## All dollar-sign entries

- Volume 053: `X$UTIL.SAM` (catalogue line 2096)
- Volume 070: `STARTRE$.NBP` (catalogue line 2843)
- Volume 134: `GLOBALS.RN$` (catalogue line 5545)
- Volume 134: `PASZCODE.RN$` (catalogue line 5547)
- Volume 177: `WS$FILES.DOC` (catalogue line 7409)
- Volume 177: `WS$MX80.ASM` (catalogue line 7410)
- Volume 177: `WS$VT52.ASM` (catalogue line 7411)

## Translation implications

Keep `/ → _` as the owner's requested starting policy, pending UI implementation/revalidation. DOS needs a separate plus-sign mapping or explicit refusal. `$` requires no default substitution for filesystem compatibility. Modern Windows allows comma, semicolon and equals, but DOS 8.3 forbids them, so they are unsuitable as universal cross-platform escape markers. A reversible mapping must handle literal escape characters and name collisions; future import cannot blindly replace every underscore with a slash.

## Reproduce

```sh
python3 tools/audit_catalog_filenames.py /path/to/-CATALOG.ALL \
  --expected-blob 8dc5b0ca05353f080e94fe3ecccf9bbb0f5d3834 \
  --output /tmp/sigm-filenames.json
```

The GitHub blob identity was checked against the complete local file. The tool parses fixed filename columns plus size/record/checksum fields and fails if candidate rows are unparsed. It does not scan descriptions for filename-like words.

## Primary references

- [Microsoft Windows filename rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file): reserved characters and device names; this is the modern long-filename profile.
- [Microsoft FAT specification, section 6.1](https://www.scs.stanford.edu/~zyedidia/docs/_other/fat.pdf): short-name byte restrictions, including plus. The document is linked, not copied into this repository.
- [POSIX definitions](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap03.html): filename/path-component rules. The conservative portable filename character set is narrower than filesystem legality.

DOS here means classic FAT short filenames, not Windows/VFAT long filenames. A broader OS, shell, network-share or cloud-sync profile may impose additional rules.
