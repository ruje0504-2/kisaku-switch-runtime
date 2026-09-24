#!/usr/bin/env python3
"""Build the external UTF-8 text table from the original MES archive.

The Windows patch replaces CP932 characters through uif_config.json.  The
runtime keeps the original bytecode, so this tool turns every translated MES
string into an explicit ``original<TAB>translated`` line.
"""
import argparse
import json
import struct
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from ai6arc import Archive

STRING_OPS = {0x0A, 0x0B, 0x33}
WORD_OPS = {0x14, 0x15, 0x16, 0x19, 0x1A, 0x32}


def read_entry(archive, entry):
    with archive.path.open("rb") as f:
        f.seek(entry.offset)
        packed = f.read(entry.packed)
    if entry.packed == entry.size:
        return packed
    ring = bytearray(4096)
    output = bytearray()
    pos, cursor = 0, 4078
    # The patch archive has up to seven bytes of compressor padding after the
    # declared output.  Stop at the directory's declared size like AI6WIN.
    while pos < len(packed) and len(output) < entry.size:
        flags = packed[pos]
        pos += 1
        for bit in range(8):
            if pos >= len(packed) or len(output) >= entry.size:
                break
            if flags & (1 << bit):
                value = packed[pos]
                pos += 1
                output.append(value)
                ring[cursor] = value
                cursor = (cursor + 1) & 4095
            else:
                lo, hi = packed[pos:pos + 2]
                pos += 2
                source = lo | ((hi & 0xF0) << 4)
                for k in range((hi & 0x0F) + 3):
                    value = ring[(source + k) & 4095]
                    output.append(value)
                    ring[cursor] = value
                    cursor = (cursor + 1) & 4095
                    if len(output) >= entry.size:
                        break
    if len(output) != entry.size:
        raise ValueError(f"truncated {entry.name}")
    return bytes(output)


def strings(data):
    count = struct.unpack_from("<I", data, 0)[0]
    pos = 4 + count * 4
    while pos < len(data):
        opcode = data[pos]
        pos += 1
        if opcode in STRING_OPS:
            end = data.find(b"\0", pos)
            if end < 0:
                return
            raw = data[pos:end]
            pos = end + 1
            try:
                yield raw.decode("cp932")
            except UnicodeDecodeError:
                continue
        elif opcode in WORD_OPS:
            pos += 4
        elif opcode == 0x1B:
            pos += 1
        if pos > len(data):
            return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("config", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8"))["character_substitution"]
    mapping = dict(zip(config["source_characters"], config["target_characters"]))
    archive = Archive(args.archive)
    pairs = dict(mapping)
    pairs.update({source: "".join(mapping.get(char, char) for char in source) for entry in archive.entries for source in strings(read_entry(archive, entry))})
    pairs = {source: target for source, target in pairs.items() if source != target}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# UTF-8 external text table generated from the Chinese patch.\n")
        f.write("# Format: original text TAB translated text. Longest matches win.\n")
        for source, target in sorted(pairs.items(), key=lambda item: (-len(item[0]), item[0])):
            f.write(f"{source}\t{target}\n")
    print(f"wrote {len(pairs)} text entries to {args.output}")


if __name__ == "__main__":
    main()
