# cpmimage

A proposed open-source, cross-platform graphical disk-image manager specializing in CP/M filesystems, targeting macOS, Windows, and Linux.

## Project status

Proposal and feasibility planning. No application implementation or release is available yet.

Read the [initial project proposal](docs/PROJECT_PROPOSAL.md) for objectives, feature priorities, proposed architecture, development phases, and open engineering questions.

## Proposed direction

- WinImage-style image-management workflows for CP/M users.
- Qt 6 Widgets, C++17 or C++20, and CMake.
- cpmtools as the proposed filesystem backend; libdsk as a candidate for additional image containers.
- Format selection from diskdefs, with automatic candidate detection and manual override.
- Reliable file operations and safe image saving for explicitly validated formats.

Backend integration and the initial format-support matrix remain provisional pending feasibility testing. An engineering specification will follow that investigation.

cpmimage is independent of BetterCP/M. BetterCP/M images will serve as useful initial compatibility cases alongside images from other CP/M systems.

## Licensing

The project is intended to be open source. License selection is pending review of the chosen dependencies and integration approach; no license has been granted yet.
