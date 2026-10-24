#!/usr/bin/env python3
"""Allow Kodi to use the console's full RAM instead of CXBE's 64 MiB default."""
from pathlib import Path
import struct
import sys


def configure(path):
    data = bytearray(path.read_bytes())
    if len(data) < 0x128 or data[:4] != b"XBEH":
        raise ValueError("Not a valid XBE header: " + str(path))
    flags, = struct.unpack_from("<I", data, 0x124)
    struct.pack_into("<I", data, 0x124, flags & ~(1 << 2))
    path.write_bytes(data)


if __name__ == "__main__":
    configure(Path(sys.argv[1]))
