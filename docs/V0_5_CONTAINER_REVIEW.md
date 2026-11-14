# v0.5 container review

On macOS ARM64, IMD, JV3 and DMK passed broker RAW-to-container Save As, file import, Save, reopen and content comparison. Unmodified cpmtools 2.23 with LibDsk extracted the expected payload directly from IMD and JV3 and from DMK after decoding it to RAW. IMD comment edits survived Save and reopen. Seven journal publication regressions passed.

Windows 11 ARM64 with MSVC Qt and Debian 13 ARM64 with GCC Qt passed offscreen format-selection smoke and the broker Save As, edit, Save, reopen and extraction checks for all three formats. x86-64 and native desktop workflows were not exercised.

## Limits and remaining qualification

Profiles are read beside an image as `<filename>.mapping.json`; structured images currently require a profile for opening and saving. A profile lists RAW slots explicitly rather than applying cpmtools skew. RAW-to-DMK encoding supports ordinary 250 kbps MFM layouts. IMD preflight rejects duplicate IDs, missing/deleted/damaged records, truncation and nonuniform or remapped sector headers outside the supported scope. JV3 uses LibDsk sector I/O; additional negative qualification of its inventory and metadata is required.

Emulator file-content verification remains a qualification gate. The initial trs80gp Model 4 runs booted the stock CP/M reference but did not verify the expected payload. A shorter key sequence reached the guest disk read and the DMK attempt reported a disk error. A retest with the BetterCP/M Model 4 boot reference yielded no verified file-content result. An emulator accepting a filename does not count as a pass.

Container-derived profile selection, expanded damaged-media fixtures and Amstrad standalone FDF qualification remain open. Batch text export remains deferred.
