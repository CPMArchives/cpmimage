# v0.4 safe writing review

Edits are staged in private RAW copies. The File menu offers Save, Save As, and Discard. The Edit menu provides file and reserved-track operations. Import review accepts batches, checks duplicate names, and offers Replace, Skip, or Cancel for existing files. Optional text import normalizes line endings to CRLF and appends a CP/M EOF marker. Export currently preserves the stored bytes.

Save As supports RAW and the qualified 40-track, single-sided CPCEMU DSK layout. LibDsk encodes the DSK and the broker verifies a byte-for-byte decoding round trip before publication. A multiply linked image requires Save As to a separate file when replacing its image bytes.

Save stages and synchronizes output and recovery copies beside the destination. If publication is interrupted, the reported recovery directory contains the manifest, original bytes and any unpublished replacements. Preserve that directory while comparing the manifest hashes with the current destination files. Restore or copy files explicitly if needed. The app does not overwrite a current image with a backup automatically. Independently rotated backups may represent different saves.

## Validation

- macOS ARM64 passed private edit and batch text/DSK tests, four publication tests, and byte-for-byte extraction with unmodified cpmtools 2.23 using the generated FDF.
- Windows 11 ARM64 passed focused Qt format-selection smoke, both broker editing tests, and all four publication tests with the rebuilt helper.
- Debian ARM64 passed the earlier Qt format-selection and RAW editing smoke. The latest DSK and batch-import changes need a fresh Debian run.
- x86-64 and native desktop publication workflows remain unqualified.

## Remaining acceptance work

Text export conversion, timestamp handling, independent qualification of DSK output, safe handling of competing publications to a shared FDF, and broader end-to-end GUI and save-failure coverage remain open.
