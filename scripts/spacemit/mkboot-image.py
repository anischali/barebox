#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Assemble a SpacemiT K1 boot media image from bootinfo and FSBL.

The bootinfo header is placed at offset 0 and the FSBL at the offsets the
BootROM reads from the bootinfo header itself (spl0_offset and, if set,
spl1_offset), so the bootinfo JSON config is the single source of truth.
"""

import argparse
import struct
import sys

BOOTINFO_MAGIC = 0xB00714F0
# magic, version, flash_type, mid, reserved, did, page_size, block_size,
# total_size, multi_plane, reserved[3], spl0_offset, spl1_offset, spl_size_limit
BOOTINFO_FMT = '<I I 4s B B H I I I B 3x I I I'


def main():
    parser = argparse.ArgumentParser(description = __doc__)
    parser.add_argument('-b', dest = 'bootinfo', required = True, help = 'bootinfo binary')
    parser.add_argument('-f', dest = 'fsbl', required = True, help = 'FSBL image')
    parser.add_argument('-o', dest = 'output', required = True, help = 'output image')
    args = parser.parse_args()

    with open(args.bootinfo, 'rb') as f:
        bootinfo = f.read()
    with open(args.fsbl, 'rb') as f:
        fsbl = f.read()

    fields = struct.unpack_from(BOOTINFO_FMT, bootinfo)
    magic, flash_type = fields[0], fields[2].rstrip(b'\0').decode(errors = 'replace')
    spl0, spl1, limit = fields[-3:]

    if magic != BOOTINFO_MAGIC:
        sys.exit(f"{args.bootinfo}: bad bootinfo magic 0x{magic:08x}")
    if len(fsbl) > limit:
        sys.exit(f"{args.fsbl}: {len(fsbl)} bytes exceeds {flash_type} "
                 f"spl_size_limit of {limit} bytes")

    offsets = [spl0] + ([spl1] if spl1 else [])
    image = bytearray(bootinfo)
    for offset in offsets:
        if offset < len(image):
            sys.exit(f"{flash_type}: FSBL offset 0x{offset:x} overlaps previous data")
        image += bytes(offset - len(image)) + fsbl

    with open(args.output, 'wb') as f:
        f.write(image)


if __name__ == '__main__':
    main()
