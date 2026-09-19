"""Read AI6WIN archives. Format reference: TesterTesterov/AI6WINArcTool.

No game data is embedded. Integers in the directory are big endian except count.
"""
from pathlib import Path
import struct
from dataclasses import dataclass


@dataclass(frozen=True)
class Entry:
    name: str
    packed: int
    size: int
    offset: int


def lzss(data: bytes, size: int) -> bytes:
    ring = bytearray(4096)
    out = bytearray()
    pos, cursor = 0, 4078
    while pos < len(data):
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if pos == len(data):
                break
            if flags & (1 << bit):
                chunk = (data[pos],)
                pos += 1
                for value in chunk:
                    out.append(value)
                    ring[cursor] = value
                    cursor = (cursor + 1) & 4095
            else:
                if pos + 2 > len(data):
                    raise ValueError('truncated LZSS reference')
                lo, hi = data[pos:pos + 2]
                pos += 2
                source = lo | ((hi & 240) << 4)
                for k in range((hi & 15) + 3):
                    value = ring[(source + k) & 4095]
                    out.append(value)
                    ring[cursor] = value
                    cursor = (cursor + 1) & 4095
            if len(out) > size:
                raise ValueError('LZSS output exceeds declared size')
    if len(out) != size:
        raise ValueError(f'LZSS size mismatch: {len(out)} != {size}')
    return bytes(out)


class Archive:
    def __init__(self, path):
        self.path = Path(path)
        self.entries = []
        length = self.path.stat().st_size
        with self.path.open('rb') as f:
            header = f.read(4)
            if len(header) != 4:
                raise ValueError('truncated archive')
            count, = struct.unpack('<I', header)
            end = 4 + count * 272
            if end > length:
                raise ValueError('directory exceeds archive')
            seen = set()
            for _ in range(count):
                raw = f.read(260).rstrip(b'\0')
                name = bytes((v - (len(raw) - i + 1)) & 255
                             for i, v in enumerate(raw)).decode('cp932')
                packed, size, offset = struct.unpack('>III', f.read(12))
                if not name or name.lower() in seen:
                    raise ValueError('empty or duplicate name')
                seen.add(name.lower())
                if offset < end or offset + packed > length:
                    raise ValueError(f'out of bounds: {name}')
                self.entries.append(Entry(name, packed, size, offset))
        intervals = sorted((e.offset, e.offset + e.packed) for e in self.entries)
        if any(b > c for (_, b), (c, _) in zip(intervals, intervals[1:])):
            raise ValueError('overlapping entries')

    def read(self, entry):
        if isinstance(entry, str):
            entry = next(e for e in self.entries if e.name.lower() == entry.lower())
        if entry.size > 256 * 1024 * 1024:
            raise ValueError('entry exceeds 256 MiB safety limit')
        with self.path.open('rb') as f:
            f.seek(entry.offset)
            data = f.read(entry.packed)
        if len(data) != entry.packed:
            raise ValueError('short read')
        return data if entry.packed == entry.size else lzss(data, entry.size)
