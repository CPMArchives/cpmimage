# Archive codec attribution

The in-memory ARC, LBR, Squeeze and Crunch routines in this directory derive
from [avwohl/80un](https://github.com/avwohl/80un), version 0.3.3, revision
`29f48645fbc60589f17895bc3db275232b6deb49`. The upstream pyproject identifies
avwohl as the author and specifies GPL-3.0-or-later, compatible with CP/M Image.
The upstream license text is included here.

CP/M Image removed the filesystem extraction and command-line utility routines,
bounded intermediate and decoded output, and tightened EOF, Huffman tree, and
checksum validation. The archive worker never uses a member name as a host path.
