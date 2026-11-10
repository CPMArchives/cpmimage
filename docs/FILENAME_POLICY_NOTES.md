# Filename translation policy notes

The local v0.2 review implements uppercase filename identities, faithful single-file export suggestions, and configurable export character mappings. Import reversal remains future work.

## Agreed configurable defaults

The owner approved these three export mappings:

| CP/M source character | Host substitute |
|---|---|
| `/` | `_` |
| `:` | `~` |
| `\` | `^` |

Provide a configuration setting where users can add, edit or remove character translations. Use the same configured mapping on every host OS, including when preparing names for DOS interchange. Keep the original CP/M filename separate from the exported filename. The local review implements these settings with a live example and saved preferences.

Leave translations for pipe, double quote and plus unresolved. Leave dollar signs and literal backticks unchanged by default. Earlier proposals for those characters are not adopted. All three agreed substitutes are absent from the audited SIG/M catalogue filenames, which reduces ambiguity for this corpus but does not guarantee reversibility for other collections.

No one-character substitution is collision-free: `SIG/M.LIB` and `SIG_M.LIB` can map to the same name. Exports must detect/refuse collisions. Reverse translation on import must be explicit, not applied blindly; guaranteed restoration requires original-name metadata or a documented escaping scheme. The current single-file workflow refuses an existing destination; it does not yet implement that future reversible scheme.

## Source-character handling

Plan host export handling for `/ : \ | "`, plus `+` for the DOS8.3 profile. Colon was discussed as a character in a source CP/M directory entry, not as a proposed replacement character. Directory entries can contain bytes that ordinary command parsers or cpmtools reject. Supporting those archive names requires retaining the original identity and validating backend access, not relabeling the input as invalid merely because a command parser reserves it. The local review uses a dedicated read-only cpmtools object that accepts colon; colon, backslash, pipe and quote listing/export have been checked against independent file bytes. The CLI tools retain their original validation.

DOS is an export naming/interchange profile, not an additional GUI platform.

## CP/M versus host rules

CP/M command processors, utilities, raw directory entries and backend libraries have different historical constraints. In particular, the pinned cpmtools2.23 `cpmdir.h` macro `ISFILECHAR` accepts printable7-bit bytes other than `< > . , ; : = ? * [ ]` (with an additional first-position space restriction). It therefore accepts slash, backslash, pipe and double quote as filename data, even though particular CP/M command parsers/manuals can impose tighter restrictions. This is backend acceptance, not a universal promise that every CP/M utility can use such a name.

| CP/M/backend filename data | macOS/Linux component | Windows long filename | Classic DOS8.3 |
|---|---|---|---|
| `/` | Not allowed | Not allowed | Not allowed |
| `\` | Accepted by POSIX APIs | Not allowed | Not allowed |
| `|` | Accepted; quote in shells | Not allowed | Not allowed |
| `"` | Accepted; quote in shells | Not allowed | Not allowed |
| `+` | Accepted | Accepted | Not allowed |
| `$` | Accepted; quote in shells | Accepted | Accepted |

Only slash and plus among these problematic characters occur in the scanned SIG/M catalogue. The current prototype's naming subset does not accept backslash, pipe or double quote; supporting them needs new fixtures and platform validation.

Colon is not a portable substitute. DOS/Windows reserve it; Finder treats it specially on macOS even though POSIX APIs and Linux can accept it. Normal CP/M references also use colon as a drive separator, and cpmtools rejects it within an ordinary filename. Comma, semicolon and equals are modern-host candidates that conflict with normal CP/M parsing, but DOS8.3 rejects them, so they do not provide a universal escape alphabet.

## References

- [Complete catalogue audit](CATALOG_FILENAME_AUDIT.md), including exact source hash and all occurrences.
- Pinned upstream source `cpmtools-2.23/cpmdir.h` and `cpmfs.c`, obtained through `packaging/dependencies.json`.
- [Microsoft Windows naming rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).
- [Microsoft FAT specification, section6.1](https://www.scs.stanford.edu/~zyedidia/docs/_other/fat.pdf).
- [POSIX definitions](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap03.html).
