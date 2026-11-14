# v0.9 package review

The four single-file candidates passed bundled `/cpmls` on the IBM 3740 fixture, listing binary.bin in user areas 0 and 7 and tail.bin in user area 15. See [SHA-256 checksums](V0_9_SHA256SUMS.txt).

| Target | GUI evidence |
| --- | --- |
| Windows 11 ARM64 | Visible application window in the signed-in desktop session |
| Windows 11 x86-64 | Opened the fixture on the desktop and displayed BINARY.BIN with four records and 512 bytes |
| Debian 13 ARM64 | Launched on KDE with the extraction cache on disk |
| Debian 13 x86-64 | Opened a visible window under Openbox on virtual X11 with bundled Python 3.13 |

The first Windows x86-64 package failed to load the Visual C++ runtime for cpmls. The reviewed executable includes the runtime beside the utilities and returned the expected fixture listing. These are smoke results. The x86-64 candidate assembly was staged locally, and reproducible release builds still need verification.

## Remaining v1.0 qualification

- Verify reproducible packages, relocation and clean-machine dependencies across the release matrix. Intel macOS remains pending qualification.
- Verify matching corresponding source, third-party notices, and release checksums.
- Complete native file-manager transfer tests, including delayed drag-out and cleanup.
- Complete platform-specific locking, Save/Save As, interruption and recovery checks.
- Verify offline Help, published online Help and link consistency, and close the remaining UI and filesystem regression items in the [roadmap](ROADMAP.md).
