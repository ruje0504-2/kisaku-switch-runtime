#!/usr/bin/env python3
"""Set the ZIP UTF-8 filename flag (general purpose bit 11) on an archive whose
entry names are already stored as raw UTF-8 bytes.

Why this exists: Apple's Info-ZIP zip 3.0 stores UTF-8 names but leaves bit 11
clear, so Windows Explorer and other tools that assume a local code page render
CJK names as mojibake.

Safety: this walks the End Of Central Directory record and the central directory
to visit the *actual* records, then patches each local header at its recorded
relative offset. It never scans for signatures in file payload, so bytes that
merely look like "PK\\x03\\x04" inside stored data are left untouched.

Usage: python3 set_zip_utf8_flag.py <archive.zip>
"""
import os
import struct
import sys

UTF8_FLAG = 0x0800
LOCAL_SIG = b"PK\x03\x04"
CENTRAL_SIG = b"PK\x01\x02"
EOCD_SIG = b"PK\x05\x06"
ZIP64_EOCD_LOCATOR_SIG = b"PK\x06\x07"
ZIP64_EOCD_SIG = b"PK\x06\x06"


def read_at(fd: int, off: int, n: int) -> bytes:
    return os.pread(fd, n, off)


def find_eocd(fd: int, size: int):
    """Return (central_offset, entry_count) for the classic or Zip64 EOCD."""
    window = min(size, 66_000)
    tail = read_at(fd, size - window, window)
    i = tail.rfind(EOCD_SIG)
    if i < 0:
        raise SystemExit("EOCD not found: not a zip archive")
    base = size - window + i
    count = struct.unpack("<H", tail[i + 10:i + 12])[0]
    cd_size = struct.unpack("<I", tail[i + 12:i + 16])[0]
    cd_off = struct.unpack("<I", tail[i + 16:i + 20])[0]

    # Zip64: locator sits 20 bytes before EOCD, or earlier with a comment.
    need64 = count == 0xFFFF or cd_off == 0xFFFFFFFF or cd_size == 0xFFFFFFFF
    if not need64:
        loc_i = tail.rfind(ZIP64_EOCD_LOCATOR_SIG)
        if loc_i < 0:
            return cd_off, count
    loc_i = tail.rfind(ZIP64_EOCD_LOCATOR_SIG)
    if loc_i < 0:
        return cd_off, count
    z64_off = struct.unpack("<Q", tail[loc_i + 8:loc_i + 16])[0]
    z = read_at(fd, z64_off, 56)
    if z[:4] != ZIP64_EOCD_SIG:
        raise SystemExit("Zip64 EOCD signature mismatch")
    count = struct.unpack("<Q", z[32:40])[0]
    cd_off = struct.unpack("<Q", z[48:56])[0]
    return cd_off, count


def patch(fd: int, path: str) -> int:
    size = os.fstat(fd).st_size
    cd_off, count = find_eocd(fd, size)
    patched = 0
    pos = cd_off
    for _ in range(count):
        head = read_at(fd, pos, 46)
        if head[:4] != CENTRAL_SIG:
            raise SystemExit(f"central directory truncated at 0x{pos:x}")
        flags = struct.unpack("<H", head[8:10])[0]
        nlen, elen, clen = struct.unpack("<HHH", head[28:34])
        local_off = struct.unpack("<I", head[42:46])[0]
        name = read_at(fd, pos + 46, nlen)

        if not flags & UTF8_FLAG:
            os.pwrite(fd, struct.pack("<H", flags | UTF8_FLAG), pos + 8)
            patched += 1

        # Zip64 extended information in the central extra field can carry the
        # real local header offset; fall back to that when it is a placeholder.
        if local_off == 0xFFFFFFFF:
            extra = read_at(fd, pos + 46 + nlen, elen)
            local_off = struct.unpack("<Q", extra[4:12])[0]

        lh = read_at(fd, local_off, 30)
        if lh[:4] == LOCAL_SIG:
            lflags = struct.unpack("<H", lh[6:8])[0]
            if not lflags & UTF8_FLAG:
                os.pwrite(fd, struct.pack("<H", lflags | UTF8_FLAG), local_off + 6)

        try:
            label = name.decode("utf-8")
        except UnicodeDecodeError:
            label = repr(name)
        if _VERBOSE:
            print(f"  {label}")
        pos += 46 + nlen + elen + clen
    return patched


def main() -> int:
    global _VERBOSE
    args = [a for a in sys.argv[1:] if a != "-v"]
    _VERBOSE = "-v" in sys.argv[1:]
    if len(args) != 1:
        print(__doc__)
        return 2
    fd = os.open(args[0], os.O_RDWR)
    try:
        n = patch(fd, args[0])
        os.fsync(fd)
    finally:
        os.close(fd)
    print(f"UTF-8 flag set on {n} central entries (local headers patched to match)")
    return 0


_VERBOSE = False

if __name__ == "__main__":
    raise SystemExit(main())
