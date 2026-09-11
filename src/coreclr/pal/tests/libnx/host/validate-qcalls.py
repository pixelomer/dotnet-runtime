#!/usr/bin/env python3
"""Check a built CoreLib against the actual QCall table in a Horizon host ELF.

Requires dnfile and pyelftools. Reads metadata and ELF data without executing
either input. This catches managed/native feature mismatches before deployment.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import dnfile
from elftools.elf.elffile import ELFFile


def validate(corelib, host):
    assembly = dnfile.dnPE(str(corelib), clr_lazy_load=True)
    managed = {str(row.ImportName) for row in assembly.net.mdtables.ImplMap.rows
               if str(row.ImportScope.row.Name) == 'QCall'}
    with host.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 64 and elf.little_endian and elf['e_machine'] == 'EM_AARCH64'
        spans = [(section['sh_addr'], section.data()) for section in elf.iter_sections()
                 if section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS']

        def read(address, size):
            for start, data in spans:
                if start <= address and address + size <= start + len(data):
                    return data[address-start:address-start+size]
            raise ValueError('Unbacked ELF address: ' + hex(address))

        symbols = [symbol for symbol in elf.get_section_by_name('.symtab').iter_symbols()
                   if symbol.name in ('s_QCall', '_ZL7s_QCall')]
        assert len(symbols) == 1, 'Need the unstripped ELF and its QCall table'
        table = symbols[0]
        assert table['st_size'] and table['st_size'] % 16 == 0
        native = set()
        for offset in range(0, table['st_size'], 16):
            name_address, function = struct.unpack('<QQ', read(table['st_value'] + offset, 16))
            assert function, 'QCall table contains a null target'
            name = bytearray()
            for index in range(256):
                byte = read(name_address + index, 1)
                if byte == b'\0':
                    break
                name.extend(byte)
            else:
                raise ValueError('Unterminated QCall name')
            native.add(name.decode('ascii'))
    return {'corelib_sha256': hashlib.sha256(corelib.read_bytes()).hexdigest(),
            'elf_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
            'managed_qcalls': len(managed), 'native_qcalls': len(native),
            'missing': sorted(managed - native)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('corelib', type=Path)
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    result = validate(args.corelib, args.elf)
    print(json.dumps(result, indent=2))
    raise SystemExit(bool(result['missing']))
