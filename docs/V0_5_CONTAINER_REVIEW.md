# v0.5 container review

On macOS ARM64, IMD, JV3 and DMK passed broker RAW-to-container Save As, file import, Save, reopen and content comparison. Unmodified cpmtools 2.23 with LibDsk extracted the expected payload directly from IMD and JV3 and from DMK after decoding it to RAW. IMD comment edits survived Save and reopen. Seven journal publication regressions passed.

Windows 11 ARM64 with MSVC Qt and Debian 13 ARM64 with GCC Qt passed offscreen format-selection smoke and the broker Save As, edit, Save, reopen and extraction checks for all three formats. x86-64 and native desktop workflows were not exercised.

## Limits and remaining qualification

Profiles are read beside an image as `<filename>.mapping.json`; structured images currently require a profile for opening and saving. A profile lists RAW slots explicitly rather than applying cpmtools skew. RAW-to-DMK encoding supports ordinary 250 kbps MFM layouts. IMD preflight rejects duplicate IDs, missing/deleted/damaged records, truncation and nonuniform or remapped sector headers outside the supported scope. JV3 uses LibDsk sector I/O; additional negative qualification of its inventory and metadata is required.

## Emulator file-content verification

On macOS ARM64, trs80gp in Model 4 mode booted stock Montezuma Micro CP/M 2.31 and read `PROOF.TXT` directly from each generated IMD, JV3, and DMK mounted in drive B. Each screen capture contained the expected file text, `CPMIMAGE INTEROP VERIFIED`. The first campaign used an 80-cylinder double-sided data layout against the stock drive configuration and returned a guest disk error. The passing campaign used the reference drive geometry: 40 cylinders, one side, eighteen 256-byte MFM sectors per track with IDs 1–18, and a standalone Montezuma system diskdef with skew 2 and two reserved tracks. Unmodified mkfs.cpm initialized the scratch RAW images, and the broker imported the proof file and published each container via Save As. No encoder change was needed for the matching configuration. This verifies native emulator reads on one representative layout per container.

Container-derived profile selection, expanded damaged-media fixtures and Amstrad standalone FDF qualification remain open. Batch text export remains deferred.
