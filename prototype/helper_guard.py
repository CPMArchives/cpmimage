# SPDX-License-Identifier: GPL-3.0-or-later
"""POSIX helper lifetime guard: orphaned helpers cannot outlive the broker.

Launched in the adapter's separate session. Helper descendants inherit that
process group. Windows uses nested kill-on-close Job Objects instead.
"""
import os
import signal
import subprocess
import sys
import time

parent = int(sys.argv[1])
if os.getppid() != parent:
    raise SystemExit(125)
child = subprocess.Popen(sys.argv[2:])
while True:
    if os.getppid() != parent:
        os.killpg(os.getpgrp(), signal.SIGKILL)
    status = child.poll()
    if status is not None:
        raise SystemExit(status if status >= 0 else 128 - status)
    time.sleep(.02)
