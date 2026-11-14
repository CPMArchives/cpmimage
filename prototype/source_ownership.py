# SPDX-License-Identifier: GPL-3.0-or-later
"""Cooperative ownership of a destination pathname and its current file identity."""
import contextlib
import hashlib
import os
from pathlib import Path
import stat


class OwnershipError(OSError):
    pass


def identity(info):
    if not stat.S_ISREG(info.st_mode) or not info.st_ino:
        raise OwnershipError('A regular file with a stable identity is required.')
    return info.st_dev, info.st_ino


@contextlib.contextmanager
def native_lock(path):
    # Keep lock files in place. Unlinking a locked file would let another process
    # create a new inode under the same lock name and acquire a second lock.
    with open(path, 'a+b') as stream:
        try:
            if os.name == 'nt':
                import msvcrt
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise OwnershipError('Cannot acquire writable ownership.') from error
        yield


class Ownership:
    """Hold both locks through replacement. Acquire the staged identity first."""

    def __init__(self, path, lock_root):
        self.requested = Path(path).absolute()
        self.path = self.requested.resolve()
        self.root = Path(lock_root)
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.locks = contextlib.ExitStack()
        self.current = None
        self.next = None
        self.current_lock = contextlib.ExitStack()
        self.next_lock = contextlib.ExitStack()
        try:
            key = os.path.normcase(str(self.path))
            if os.name == 'posix':
                # On case-insensitive volumes, differently cased pathnames can
                # refer to the same entry, including during replacement.
                key = key.casefold()
            self.locks.enter_context(self._lock('path', key))
            self.current = self._read_identity()
            if self.current is not None:
                self.current_lock.enter_context(self._lock('file', self.current))
            self.check()
        except BaseException:
            self.close()
            raise

    def _lock(self, kind, key):
        digest = hashlib.sha256(repr((kind, key)).encode('utf-8', 'surrogatepass')).hexdigest()
        return native_lock(self.root / digest)

    def _read_identity(self):
        try:
            return identity(self.path.stat())
        except FileNotFoundError:
            return None

    def check(self, *, replacing=False):
        if self.requested.resolve() != self.path or self._read_identity() != self.current:
            raise OwnershipError('The destination identity changed.')
        if replacing and self.current is not None and self.path.stat().st_nlink > 1:
            raise OwnershipError('This image has multiple hard links. Use Save As to a new path.')

    def reserve_replacement(self, staged):
        if self.next is not None:
            raise OwnershipError('A replacement identity is already reserved.')
        candidate = identity(Path(staged).stat())
        self.next_lock.enter_context(self._lock('file', candidate))
        self.next = candidate

    def adopt_replacement(self):
        if self.next is None or self._read_identity() != self.next:
            raise OwnershipError('The published image does not have the reserved identity.')
        self.current, self.next = self.next, None
        self.current_lock.close()
        self.current_lock = self.next_lock.pop_all()

    def close(self):
        self.next_lock.close()
        self.current_lock.close()
        self.locks.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
