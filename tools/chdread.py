# SPDX-License-Identifier: GPL-3.0-or-later
"""Random-access reader for hard-disk CHDs via libchdr (libchdr_mini.so).

Used for Konami Python disks (20 GB logical, ~3 GB of data), which are read in place
rather than extracted with chdman (tools/chd.py).

    from chdread import Chd
    d = Chd(path); d.read(byte_offset, length); d.sector(lba)
Only reads hunks on demand (LRU cache), so huge images are never extracted.
"""
import ctypes, os, functools

def _find_lib():
    """$HK_LIBCHDR, else build/libchdr_mini.so (Windows: .dll) in this checkout or package
    (tools/build-libchdr.sh or CMake's chdr_mini makes it), else a Windows build's own folder."""
    here = os.path.dirname(os.path.abspath(__file__))
    name = "libchdr_mini.dll" if os.name == "nt" else "libchdr_mini.so"
    for p in (os.environ.get("HK_LIBCHDR", ""), os.path.join(here, "..", "build", name),
              os.path.join(here, "..", "build", "release-windows", name)):
        if p and os.path.exists(p):
            return p
    raise OSError("libchdr_mini.so not found: run tools/build-libchdr.sh, or set HK_LIBCHDR")


_lib = ctypes.CDLL(_find_lib())
_lib.chd_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
_lib.chd_read.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p]
_lib.chd_close.argtypes = [ctypes.c_void_p]


class Chd:
    def __init__(self, path, hunk=4096):
        self.h = ctypes.c_void_p()
        err = _lib.chd_open(path.encode(), 1, None, ctypes.byref(self.h))
        if err:
            raise OSError(f"chd_open {path}: error {err}")
        self.hunk = hunk
        self._buf = ctypes.create_string_buffer(hunk)
        self._cache = {}

    def _hunk(self, n):
        c = self._cache.get(n)
        if c is None:
            err = _lib.chd_read(self.h, n, self._buf)
            if err:
                raise OSError(f"chd_read hunk {n}: error {err}")
            c = self._buf.raw
            if len(self._cache) > 4096:
                self._cache.clear()
            self._cache[n] = c
        return c

    def read(self, off, n):
        out = bytearray()
        while n > 0:
            h, o = divmod(off, self.hunk)
            take = min(n, self.hunk - o)
            out += self._hunk(h)[o:o + take]
            off += take
            n -= take
        return bytes(out)

    def sector(self, lba, count=1):
        return self.read(lba * 512, count * 512)

    def close(self):
        _lib.chd_close(self.h)
