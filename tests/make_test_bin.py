#!/usr/bin/env python3
"""Create a fake 8 KB application binary linked for slot B (valid SP + reset vector)."""
import struct
import sys

code_start = 0x08080000 + 0x200
data = bytearray((i * 13) & 0xFF for i in range(8192))
struct.pack_into("<II", data, 0, 0x20020000, (code_start + 0x189) | 1)
with open(sys.argv[1], "wb") as f:
    f.write(data)
