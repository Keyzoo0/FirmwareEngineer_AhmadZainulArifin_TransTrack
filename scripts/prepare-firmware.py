#!/usr/bin/env python3
"""
prepare-firmware.py - wrap a raw application binary into a bootable slot image.

Output layout (matches common/inc/fw_image.h and IMAGE_HEADER_SIZE in flash_layout.h):

    offset 0x000  fw_image_header_t (128 bytes, little-endian, packed)
    offset 0x080  0xFF padding up to 0x200
    offset 0x200  application binary (vector table first)

The header carries a CRC-32 (zlib polynomial, same as common/src/crc32.c) and a SHA-256
of the binary; the bootloader recomputes both before it will boot or switch to the slot.

Examples
    # image for OTA (send-firmware.py) or for flashing at the slot base address
    scripts/prepare-firmware.py build/app_slot_b.bin --slot b --version 1.2.0 -o app_b_v1.2.0.img

    # full flash image: bootloader + slot A, ready for st-flash write ... 0x08000000
    scripts/prepare-firmware.py build/app_slot_a.bin --slot a --version 1.1.0 \
        --bootloader build/bootloader.bin -o factory.bin
"""
import argparse
import hashlib
import struct
import subprocess
import sys
import time
import zlib

MAGIC = 0x57465454            # "TTFW"
HEADER_VERSION = 1
HEADER_SIZE = 0x200
HEADER_STRUCT = struct.Struct("<IHBBBBHIII32s16s52s")   # 124 bytes, then header_crc32
SLOTS = {"a": (0, 0x08020000), "b": (1, 0x08080000)}
SLOT_SIZE = 384 * 1024
FLASH_BASE = 0x08000000
BOOTLOADER_SIZE = 32 * 1024
RAM_START, RAM_END = 0x20000000, 0x20020000


def parse_version(text):
    parts = text.split(".")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("version must be MAJOR.MINOR.PATCH")
    major, minor, patch = (int(p) for p in parts)
    if not (0 <= major < 256 and 0 <= minor < 256 and 0 <= patch < 65536):
        raise argparse.ArgumentTypeError("version component out of range")
    return major, minor, patch


def git_rev():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short=12", "HEAD"],
                                       stderr=subprocess.DEVNULL).decode().strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def build_header(slot_index, version, image, build_time, rev):
    major, minor, patch = version
    body = HEADER_STRUCT.pack(
        MAGIC, HEADER_VERSION, slot_index, 0,
        major, minor, patch,
        len(image), zlib.crc32(image) & 0xFFFFFFFF, build_time,
        hashlib.sha256(image).digest(),
        rev.encode()[:16].ljust(16, b"\0"),
        b"\0" * 52)
    assert len(body) == 124
    header = body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    return header + b"\xFF" * (HEADER_SIZE - len(header))


def check_vectors(image, slot_base):
    """Refuse binaries linked for the other slot - they would jump into the wrong image."""
    if len(image) < 8:
        sys.exit("error: binary too small")
    sp, reset = struct.unpack_from("<II", image, 0)
    code_start = slot_base + HEADER_SIZE
    if not (RAM_START <= sp <= RAM_END) or sp & 3:
        sys.exit(f"error: initial SP 0x{sp:08x} is not in SRAM")
    if not (reset & 1) or not (code_start <= (reset & ~1) < code_start + len(image)):
        sys.exit(f"error: reset handler 0x{reset:08x} is outside slot at 0x{code_start:08x} "
                 "(binary linked for the other slot?)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary", help="raw application binary (objcopy -O binary)")
    ap.add_argument("--slot", required=True, choices=SLOTS.keys(), help="slot the binary was linked for")
    ap.add_argument("--version", required=True, type=parse_version, help="MAJOR.MINOR.PATCH")
    ap.add_argument("--bootloader", help="also emit a full flash image starting at 0x08000000")
    ap.add_argument("--build-time", type=int, default=None, help="unix time (default: now)")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    with open(args.binary, "rb") as f:
        image = f.read()
    slot_index, slot_base = SLOTS[args.slot]
    if len(image) > SLOT_SIZE - HEADER_SIZE:
        sys.exit(f"error: image is {len(image)} bytes, slot holds {SLOT_SIZE - HEADER_SIZE}")
    check_vectors(image, slot_base)

    build_time = args.build_time if args.build_time is not None else int(time.time())
    slot_image = build_header(slot_index, args.version, image, build_time, git_rev()) + image

    if args.bootloader:
        with open(args.bootloader, "rb") as f:
            bl = f.read()
        if len(bl) > BOOTLOADER_SIZE:
            sys.exit("error: bootloader larger than 32 KB")
        out = bytearray(b"\xFF" * (slot_base - FLASH_BASE))
        out[:len(bl)] = bl
        out += slot_image
        data = bytes(out)
    else:
        data = slot_image

    with open(args.output, "wb") as f:
        f.write(data)
    print(f"{args.output}: slot {args.slot.upper()} v{'.'.join(map(str, args.version))}, "
          f"{len(image)} B, crc32=0x{zlib.crc32(image) & 0xFFFFFFFF:08x}, "
          f"sha256={hashlib.sha256(image).hexdigest()[:16]}...")


if __name__ == "__main__":
    main()
