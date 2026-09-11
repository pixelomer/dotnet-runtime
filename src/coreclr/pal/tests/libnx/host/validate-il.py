#!/usr/bin/env python3
"""Reject unsupported native/mixed/foreign-architecture CoreCLR deployment inputs.

Requires pefile (also installed with dnfile). This checks deployment format, not safety or BCL compatibility.
"""
import argparse
from pathlib import Path
import struct
import pefile


def validate(path):
    pe = pefile.PE(str(path), fast_load=True)
    try:
        directories = pe.OPTIONAL_HEADER.DATA_DIRECTORY
        if len(directories) <= 14 or not directories[14].VirtualAddress or directories[14].Size < 72:
            raise ValueError('not a managed assembly')
        header = pe.get_data(directories[14].VirtualAddress, 72)
        if len(header) != 72 or struct.unpack_from('<I', header)[0] < 72:
            raise ValueError('invalid managed header')
        flags = struct.unpack_from('<I', header, 16)[0]
        native_rva, native_size = struct.unpack_from('<II', header, 64)
        if native_rva or native_size:
            raise ValueError('native/ReadyToRun image; publish IL with PublishReadyToRun=false')
        if not flags & 1 or flags & (2 | 16):
            raise ValueError('mixed native code, 32-bit requirement or native entry point')
        machine = pe.FILE_HEADER.Machine
        if machine not in (0x14c, 0xaa64):
            raise ValueError('requires a foreign CPU architecture')
        if machine == 0x14c and pe.OPTIONAL_HEADER.Magic != 0x10b:
            raise ValueError('invalid AnyCPU PE format')
        if machine == 0xaa64 and pe.OPTIONAL_HEADER.Magic != 0x20b:
            raise ValueError('invalid ARM64 PE format')
    finally:
        pe.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assemblies', nargs='+', type=Path)
    args = parser.parse_args()
    for path in args.assemblies:
        try:
            validate(path)
        except (ValueError, OSError, pefile.PEFormatError) as error:
            parser.exit(1, f'{path}: {error}\n')
    print(f'IL deployment format verified: {len(args.assemblies)} assemblies')
