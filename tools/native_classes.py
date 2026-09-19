"""Inventory 32-bit MSVC classes and vtables, without executing the PE.

Usage: local/venv/bin/python tools/native_classes.py 鬼作/AI6WIN.exe
Requires pefile. Output contains addresses and class names, no game assets.
"""
import argparse
import hashlib
import json
import re
import struct
import pefile


def inventory(path):
    pe = pefile.PE(path)
    data = bytes(pe.__data__)
    base = pe.OPTIONAL_HEADER.ImageBase
    executable = [(base + s.VirtualAddress, base + s.VirtualAddress + s.Misc_VirtualSize)
                  for s in pe.sections if s.Characteristics & 0x20000000]
    def is_code(value):
        return any(start <= value < end for start, end in executable)
    classes = []
    for match in re.finditer(rb'\.\?AV[^\x00]{1,160}\x00', data):
        descriptor = pe.get_rva_from_offset(match.start()) + base - 8
        tables = []
        for colref in re.finditer(re.escape(struct.pack('<I', descriptor)), data):
            offset = colref.start() - 12
            if offset < 0:
                continue
            signature, displacement, constructor, _, hierarchy = struct.unpack_from('<5I', data, offset)
            if signature != 0 or displacement > 0x100000 or constructor > 0x100000:
                continue
            if hierarchy < base or hierarchy >= base + pe.OPTIONAL_HEADER.SizeOfImage:
                continue
            locator = pe.get_rva_from_offset(offset) + base
            for reference in re.finditer(re.escape(struct.pack('<I', locator)), data):
                address = pe.get_rva_from_offset(reference.start()) + base + 4
                methods = []
                for index in range(256):
                    entry = pe.get_data(address - base + index * 4, 4)
                    if len(entry) != 4:
                        break
                    target = struct.unpack('<I', entry)[0]
                    if not is_code(target):
                        break
                    methods.append(hex(target))
                if methods:
                    tables.append({'address': hex(address), 'this_offset': displacement,
                                   'methods': methods})
        if tables:
            classes.append({'name': match.group()[:-1].decode('ascii'), 'vtables': tables})
    return {'exe_sha256': hashlib.sha256(data).hexdigest(), 'image_base': hex(base),
            'classes': classes}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe')
    args = parser.parse_args()
    print(json.dumps(inventory(args.exe), indent=2))
