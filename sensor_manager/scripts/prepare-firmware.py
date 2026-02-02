#!/usr/bin/env python3
"""
STM32 Sensor Manager - Firmware Preparation Script

This script adds a header to the STM32 firmware binary containing:
- Version information
- Binary size
- CRC32 checksum

Usage:
    python prepare-firmware.py input.elf output.bin [--version 1.0.0]

Requirements:
    - arm-none-eabi-objcopy (from ARM GNU Toolchain)
"""

import argparse
import struct
import subprocess
import sys
import os
from pathlib import Path


def calculate_crc32(data: bytes) -> int:
    """
    Calculate CRC32 checksum of binary data.

    Args:
        data: Binary data as bytes

    Returns:
        CRC32 checksum as 32-bit unsigned integer
    """
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


def extract_version_from_args(args) -> tuple:
    """
    Extract version from command line arguments or use defaults.

    Args:
        args: Parsed command line arguments

    Returns:
        Tuple of (major, minor, patch) version numbers
    """
    if args.version:
        try:
            major, minor, patch = map(int, args.version.split('.'))
            return major, minor, patch
        except ValueError:
            print(f"Error: Invalid version format '{args.version}'. Use X.Y.Z format.")
            sys.exit(1)

    # Default version
    return 1, 0, 0


def elf_to_bin(elf_path: Path, bin_path: Path) -> bool:
    """
    Convert ELF file to binary using arm-none-eabi-objcopy.

    Args:
        elf_path: Path to input ELF file
        bin_path: Path to output binary file

    Returns:
        True if successful, False otherwise
    """
    try:
        subprocess.run([
            'arm-none-eabi-objcopy',
            '-O', 'binary',
            str(elf_path),
            str(bin_path)
        ], check=True, capture_output=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error: objcopy failed: {e.stderr.decode()}")
        return False
    except FileNotFoundError:
        print("Error: arm-none-eabi-objcopy not found. Please install ARM GNU Toolchain.")
        return False


def add_firmware_header(input_bin: Path, output_path: Path,
                        version_major: int, version_minor: int,
                        version_patch: int) -> bool:
    """
    Add firmware header to binary file.

    Header format (16 bytes total):
    [0-3]   Magic: 'F','W','H','R' (Firmware HeaDeR)
    [4-5]   Version Major (uint16_t)
    [6-7]   Version Minor (uint16_t)
    [8-9]   Version Patch (uint16_t)
    [10-13] Binary Size (uint32_t)
    [14-17] CRC32 Checksum (uint32_t)

    Args:
        input_bin: Path to input binary file
        output_path: Path to output file with header
        version_major: Major version number
        version_minor: Minor version number
        version_patch: Patch version number

    Returns:
        True if successful, False otherwise
    """
    try:
        # Read binary data
        with open(input_bin, 'rb') as f:
            binary_data = f.read()

        # Calculate size and CRC
        binary_size = len(binary_data)
        crc32 = calculate_crc32(binary_data)

        # Create header
        header = struct.pack(
            '<4BHHHI I',  # Little-endian format
            ord('F'), ord('W'), ord('H'), ord('R'),  # Magic
            version_major, version_minor, version_patch,
            binary_size,
            crc32
        )

        # Write header + binary data to output
        with open(output_path, 'wb') as f:
            f.write(header)
            f.write(binary_data)

        # Print summary
        print(f"Firmware prepared successfully!")
        print(f"  Version: {version_major}.{version_minor}.{version_patch}")
        print(f"  Size: {binary_size} bytes ({binary_size / 1024:.2f} KB)")
        print(f"  CRC32: 0x{crc32:08X}")
        print(f"  Output: {output_path}")

        return True

    except FileNotFoundError:
        print(f"Error: Input file '{input_bin}' not found.")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False


def verify_firmware(firmware_path: Path) -> bool:
    """
    Verify firmware header and CRC32 checksum.

    Args:
        firmware_path: Path to firmware file with header

    Returns:
        True if verification successful, False otherwise
    """
    try:
        with open(firmware_path, 'rb') as f:
            header = f.read(16)
            if len(header) < 16:
                print("Error: File too small to contain header")
                return False

            # Parse header
            magic, major, minor, patch, size, crc32 = struct.unpack('<4BHHHI I', header)

            # Verify magic
            if chr(magic[0]) != 'F' or chr(magic[1]) != 'W' or \
               chr(magic[2]) != 'H' or chr(magic[3]) != 'R':
                print("Error: Invalid magic number in header")
                return False

            # Read binary data
            binary_data = f.read()

            # Verify size
            if len(binary_data) != size:
                print(f"Error: Size mismatch. Header says {size}, actual {len(binary_data)}")
                return False

            # Verify CRC
            calculated_crc = calculate_crc32(binary_data)
            if calculated_crc != crc32:
                print(f"Error: CRC mismatch. Header: 0x{crc32:08X}, Calculated: 0x{calculated_crc:08X}")
                return False

            print(f"Firmware verification successful!")
            print(f"  Version: {major}.{minor}.{patch}")
            print(f"  Size: {size} bytes")
            print(f"  CRC32: 0x{crc32:08X} (verified)")

            return True

    except Exception as e:
        print(f"Error during verification: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description='Prepare STM32 firmware with header and checksum'
    )
    parser.add_argument('input', type=Path, help='Input .elf or .bin file')
    parser.add_argument('output', type=Path, help='Output .bin file')
    parser.add_argument('--version', type=str, default='1.0.0',
                       help='Firmware version in X.Y.Z format (default: 1.0.0)')
    parser.add_argument('--verify', action='store_true',
                       help='Verify firmware file instead of creating new one')
    parser.add_argument('--no-convert', action='store_true',
                       help='Skip ELF to binary conversion (input is already .bin)')

    args = parser.parse_args()

    if args.verify:
        return 0 if verify_firmware(args.input) else 1

    # Convert ELF to BIN if needed
    if not args.no_convert and args.input.suffix == '.elf':
        temp_bin = args.input.with_suffix('.temp.bin')
        if not elf_to_bin(args.input, temp_bin):
            return 1
        input_bin = temp_bin
    else:
        input_bin = args.input

    # Extract version
    major, minor, patch = extract_version_from_args(args)

    # Add header
    success = add_firmware_header(input_bin, args.output, major, minor, patch)

    # Clean up temp file
    if not args.no_convert and args.input.suffix == '.elf' and 'temp_bin' in locals():
        temp_bin.unlink(missing_ok=True)

    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
