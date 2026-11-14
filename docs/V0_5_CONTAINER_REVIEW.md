# v0.5 completion report

On macOS ARM64, IMD, JV3 and DMK passed broker RAW-to-container Save As, file import, Save, reopen and content comparison. Unmodified cpmtools 2.23 with LibDsk extracted the expected payload directly from IMD and JV3 and from DMK after decoding it to RAW. IMD comment edits survived Save and reopen. Seven journal publication regressions passed.

Windows 11 ARM64 with MSVC Qt and Debian 13 ARM64 with GCC Qt passed offscreen format-selection smoke and the broker Save As, edit, Save, reopen and extraction checks for all three formats. x86-64 and native desktop workflows were not exercised.

## Implemented capabilities and limits

Profiles are read beside an image as `<filename>.mapping.json`; canonical single-sided images can open without one. A profile lists RAW slots explicitly rather than applying cpmtools skew. RAW-to-DMK encoding supports ordinary 250 kbps MFM layouts. IMD preflight rejects duplicate IDs, missing/deleted/damaged records, truncation and nonuniform or remapped sector headers outside the supported scope. JV3 uses LibDsk for sector I/O. A descriptor preflight checks the complete inventory before opening and again before publishing encoded output. Focused macOS ARM64 broker tests reject CRC-error flags, deleted data marks, duplicate IDs, truncated payloads and density mismatches with explicit diagnostics and preserve the input bytes. The gate also rejects unsupported write protection, data marks and short-sector flags.

## Emulator file-content verification

On macOS ARM64, trs80gp in Model 4 mode booted stock Montezuma Micro CP/M 2.31 and read `PROOF.TXT` directly from each generated IMD, JV3, and DMK mounted in drive B. Each screen capture contained the expected file text, `CPMIMAGE INTEROP VERIFIED`. The first campaign used an 80-cylinder double-sided data layout against the stock drive configuration and returned a guest disk error. The passing campaign used the reference drive geometry: 40 cylinders, one side, eighteen 256-byte MFM sectors per track with IDs 1–18, and a standalone Montezuma system diskdef with skew 2 and two reserved tracks. Unmodified mkfs.cpm initialized the scratch RAW images, and the broker imported the proof file and published each container via Save As. No encoder change was needed for the matching configuration. This verifies native emulator reads on one representative layout per container.

Profile-free open is limited to single-sided tracks numbered contiguously from zero, with the same ordered, contiguous zero- or one-based sector IDs and uniform recording mode. Sizes and counts must match the selected diskdef. The full container validation still runs, including DMK CRC checks and JV3 inventory checks. cpmtools alone applies filesystem skew. Noncanonical ordering and multi-sided layouts need an explicit profile. Save to the current image reuses its verified mapping. Save As to IMD, JV3 or DMK still requires a destination profile. RAW and standard CPCEMU DSK outputs require none.

Focused macOS ARM64 checks opened, edited, saved and reopened all three containers after removing their profiles, and independently extracted the expected bytes. Mapping tests rejected a noncanonical IMD order without a profile. The JV3 negative and seven publication tests also passed. No additional cross-platform run was performed.

Automatic standalone FDF generation for the boot-derived `amstrad` definition still returns an explicit unsupported-capability error. Broader container-derived mapping and batch or whole-user-area text export remain deferred.
