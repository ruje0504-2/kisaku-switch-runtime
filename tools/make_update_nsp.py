#!/usr/bin/env python3
"""Build a real Switch Patch NSP from a base NSP and new ExeFS/RomFS.

This follows the update layout used by the reference runtime project.  The
local hacbrewpack is used for ordinary NCA encryption, then the Program NCA is
rewritten to carry a BKTR RomFS overlay and the metadata NCA is rewritten to a
Patch CNMT.

Two RomFS modes are supported:

* default (no ``--romfsdir``) -- zero-change overlay: the whole virtual image is
  mapped back to the base RomFS, so the update only ships ExeFS.
* ``--romfsdir DIR`` -- delta overlay: the new RomFS image is built from ``DIR``
  and every file whose bytes still match the base NSP maps straight back to the
  base NCA; only new/changed file data and the re-hashed IVFC tables are stored
  in the patch.  This is what lets the update ship just the new font/config
  while the bulky original archives keep coming from the base NSP.

The BKTR FS header (``fs_header[1]``) must contain, after the 8-byte section
type prefix and the IVFC header:

    +0x000  version/partition/fs/crypt type (``02 00 00 03 04``)
    +0x008  IVFC header (0xE0)          -- describes the *new* virtual image
    +0x0E8  0x18 reserved
    +0x100  relocation header (0x20)    -- offset/size/magic/version/entries
    +0x120  subsection header (0x20)    -- offset/size/magic/version/entries
    +0x140  section_ctr (8)             -- keeps BKTR CTR == plain section CTR

Writing those two table headers is the difference between "hactool recognizes
the partition type" and "hactool can actually overlay-read the base".

Section layout produced here (``P`` = patch data length):

    0x0000                        patch data (new/changed data only)
    P                             relocation block header + buckets
    P + 0x4000 * (1 + n_buckets)  subsection block header + bucket

The patch data deliberately lives *before* both table regions: hactool and
LibHac size the relocation/subsection regions from the headers and load them
whole, so anything parked between the relocation block and the subsection block
would be read back as bucket data.  For the reference zero-change patch (P = 0)
this reduces to relocation at 0 and subsection at 0x8000, exactly the verified
sample.  ``relocation_header.offset + relocation_header.size ==
subsection_header.offset`` and ``subsection_header.offset +
subsection_header.size == section_size`` are enforced by hactool and yuzu.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from nca_crypto import aes_ctr, aes_xts_sector, aes_decrypt_block
except ImportError:
    from tools.nca_crypto import aes_ctr, aes_xts_sector, aes_decrypt_block

CT_PROGRAM, CT_CONTROL = 1, 3
META_TYPE_APPLICATION = 0x80
META_TYPE_PATCH = 0x81
CNMT_HEADER_FMT = "<QIBBHHHBBBBII"
PATCH_EXT_FMT = "<QIIQ"

MAGIC_BKTR = 0x52544B42
MAGIC_IVFC = 0x43465649
BKTR_BLOCK_SIZE = 0x4000
BKTR_BLOCK_LOG2 = 14
# bktr_header_t { u64 offset; u64 size; u32 magic; u32 version; u32 entries; u32 rsvd; }
BKTR_HEADER_FMT = "<QQIIII"
# bktr_relocation_entry_t { u64 virt_offset; u64 phys_offset; u32 is_patch; }
RELOC_ENTRY_FMT = "<QQI"
RELOC_ENTRY_SIZE = 0x14
RELOC_BUCKET_ENTRIES = (0x4000 - 0x10) // RELOC_ENTRY_SIZE   # 818, one slot is the sentinel
RELOC_BUCKET_FMT = "<IIQ"
# bktr_subsection_entry_t { u64 offset; u32 _0x8; u32 ctr_val; }
SUBSEC_ENTRY_FMT = "<QII"
SUBSEC_BUCKET_FMT = "<IIQ"

IMAGE_CHUNK = 1 << 22          # 4 MiB, a multiple of the 0x4000 block size


def die(message: str) -> None:
    raise SystemExit(message)


def parse_keys(path: str) -> dict[str, bytes]:
    out: dict[str, bytes] = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = (p.strip() for p in line.split("=", 1))
        try:
            out[name] = bytes.fromhex(value)
        except ValueError:
            continue
    return out


def parse_version(text: str) -> int:
    if text.lower().startswith("0x"):
        return int(text, 16)
    parts = [int(p) for p in text.split(".")]
    parts += [0] * (3 - len(parts))
    return (parts[0] << 16) | (parts[1] << 8) | parts[2]


def version_display(text: str, value: int) -> bytes:
    if not text.lower().startswith("0x"):
        return text.encode()[:0x10]
    return (f"{(value >> 16) & 0xff}.{(value >> 8) & 0xff}.{value & 0xff}").encode()


# --------------------------------------------------------------------------
# PFS0 / NCA helpers
# --------------------------------------------------------------------------

def pfs0_members(path: str) -> list[tuple[str, int, int]]:
    with open(path, "rb") as fh:
        magic, count, strsize, _ = struct.unpack("<4sIII", fh.read(0x10))
        if magic != b"PFS0":
            die(f"不是 PFS0: {path}")
        entries = [struct.unpack("<QQI4x", fh.read(0x18)) for _ in range(count)]
        strings = fh.read(strsize)
    return [(strings[nameoff:strings.index(b"\0", nameoff)].decode(), off, size)
            for off, size, nameoff in entries]


def read_member(path: str, name: str, size: int | None = None) -> bytes:
    with open(path, "rb") as fh:
        magic, count, strsize, _ = struct.unpack("<4sIII", fh.read(0x10))
        if magic != b"PFS0":
            die(f"不是 PFS0: {path}")
        entries = [struct.unpack("<QQI4x", fh.read(0x18)) for _ in range(count)]
        strings = fh.read(strsize)
        data0 = 0x10 + count * 0x18 + strsize
        for off, length, nameoff in entries:
            current = strings[nameoff:strings.index(b"\0", nameoff)].decode()
            if current == name:
                fh.seek(data0 + off)
                return fh.read(length if size is None else min(size, length))
    die(f"NSP 中没有 {name}")


def member_data_offset(path: str, name: str) -> int:
    """Absolute file offset of a PFS0 member's data inside the NSP."""
    with open(path, "rb") as fh:
        magic, count, strsize, _ = struct.unpack("<4sIII", fh.read(0x10))
        if magic != b"PFS0":
            die(f"不是 PFS0: {path}")
        entries = [struct.unpack("<QQI4x", fh.read(0x18)) for _ in range(count)]
        strings = fh.read(strsize)
        data0 = 0x10 + count * 0x18 + strsize
        for off, _length, nameoff in entries:
            if strings[nameoff:strings.index(b"\0", nameoff)].decode() == name:
                return data0 + off
    die(f"NSP 中没有 {name}")


def decrypt_header(raw: bytes, header_key: bytes) -> bytes:
    if len(raw) < 0xc00:
        die("NCA 头不足 0xC00 字节")
    out = bytearray(raw[:0xc00])
    for sector in range(1, 6):
        start = sector * 0x200
        out[start:start + 0x200] = aes_xts_sector(
            header_key, raw[start:start + 0x200], sector, True)
    return bytes(out)


def encrypt_header(header: bytes, header_key: bytes) -> bytes:
    out = bytearray(header[:0xc00])
    for sector in range(1, 6):
        start = sector * 0x200
        out[start:start + 0x200] = aes_xts_sector(
            header_key, header[start:start + 0x200], sector, False)
    return bytes(out)


def section_range(header: bytes, index: int) -> tuple[int, int]:
    return struct.unpack_from("<II", header, 0x240 + index * 0x10)


def section_key(header: bytes, keys: dict[str, bytes]) -> bytes:
    """Section key for hacbrewpack-generated NCAs (key-area slot 2)."""
    keygen = header[0x207]
    name = f"key_area_key_application_{keygen:02x}"
    area_key = keys.get(name)
    if area_key is None:
        die(f"prod.keys 缺少 {name}")
    return aes_decrypt_block(area_key, header[0x320:0x330])


def section_ctr(section_start_units: int) -> bytes:
    return (section_start_units * 0x200 // 0x10).to_bytes(16, "big")


# --------------------------------------------------------------------------
# base section plaintext access
# --------------------------------------------------------------------------

def iter_section_plaintext(path: str, key: bytes, nca_off: int, size: int,
                           nca_base: int = 0):
    """Yield the decrypted byte stream of one NCA section.

    ``nca_off`` is the section offset *inside the NCA* and drives the AES-128-CTR
    counter (offset >> 4, big-endian), exactly like hactool's CTR path.
    ``nca_base`` is where the NCA itself starts in ``path``, which lets the base
    section be streamed straight out of the NSP member without extracting a
    multi-GB NCA first.  pycryptodome is used when importable, otherwise we
    stream through the ``openssl`` CLI (the reference implementation's route).
    """
    aligned = nca_off & ~0xF
    skip = nca_off - aligned
    read_start = nca_base + aligned
    counter = aligned >> 4
    try:
        from Crypto.Cipher import AES          # type: ignore
        from Crypto.Util import Counter        # type: ignore
    except Exception:
        AES = None

    if AES is not None:
        cipher = AES.new(key, AES.MODE_CTR, counter=Counter.new(128, initial_value=counter))
        with open(path, "rb") as fh:
            fh.seek(read_start)
            remaining = size + skip
            while remaining > 0:
                chunk = fh.read(min(IMAGE_CHUNK, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
                data = cipher.decrypt(chunk)
                if skip:
                    data = data[skip:]
                    skip = 0
                if data:
                    yield data
        return

    iv = counter.to_bytes(16, "big").hex()
    proc = subprocess.Popen(
        ["openssl", "enc", "-d", "-aes-128-ctr", "-K", key.hex(), "-iv", iv],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        with open(path, "rb") as fh:
            fh.seek(read_start)
            remaining = size + skip
            while remaining > 0:
                chunk = fh.read(min(1 << 20, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
                proc.stdin.write(chunk)
        proc.stdin.close()
        while True:
            chunk = proc.stdout.read(IMAGE_CHUNK)
            if not chunk:
                break
            if skip:
                chunk = chunk[skip:]
                skip = 0
            if chunk:
                yield chunk
    finally:
        proc.stdout.close()
        proc.wait()


# --------------------------------------------------------------------------
# RomFS parsing / splicing
# --------------------------------------------------------------------------

def parse_romfs_files(header: bytes, meta: bytes) -> dict[str, tuple[int, int]]:
    """``name -> (offset relative to the image start, size)``."""
    data_offset = struct.unpack_from("<Q", header, 0x48)[0]
    files: dict[str, tuple[int, int]] = {}
    off = 0
    while off + 0x20 <= len(meta):
        _parent, _sibling, file_off, file_size, _hash, name_size = struct.unpack_from(
            "<IIQQII", meta, off)
        name = meta[off + 0x20:off + 0x20 + name_size].decode("utf-8", "replace")
        files[name] = (data_offset + file_off, file_size)
        off += 0x20 + ((name_size + 3) // 4) * 4
    return files


def read_local_romfs(image_path: str) -> tuple[bytes, dict[str, tuple[int, int]]]:
    with open(image_path, "rb") as fh:
        header = fh.read(0x50)
        fm_off, fm_size = struct.unpack_from("<QQ", header, 0x38)
        fh.seek(fm_off)
        meta = fh.read(fm_size)
    return header, parse_romfs_files(header, meta)


def read_local_romfs_files(image_path: str) -> dict[str, tuple[int, int]]:
    return read_local_romfs(image_path)[1]


def read_file_range(path: str, offset: int, size: int) -> bytes:
    with open(path, "rb") as fh:
        fh.seek(offset)
        return fh.read(size)


def hash_file_range(path: str, offset: int, size: int) -> bytes:
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        fh.seek(offset)
        remaining = size
        while remaining > 0:
            chunk = fh.read(min(IMAGE_CHUNK, remaining))
            if not chunk:
                break
            remaining -= len(chunk)
            digest.update(chunk)
    return digest.digest()


# --------------------------------------------------------------------------
# IVFC image construction (romfs image -> levels L0..L5)
# --------------------------------------------------------------------------

def _hash_level(reader, size: int, block_log2: int = BKTR_BLOCK_LOG2) -> bytes:
    """Build a hash table over ``size`` bytes of ``1 << block_log2`` blocks.

    The final partial block is zero padded before hashing and the table itself
    is padded to a whole 0x4000 block, matching how the base NCA was produced.
    """
    span = 1 << block_log2
    out = bytearray()
    for off in range(0, size, span):
        block = reader(off, min(span, size - off))
        if len(block) < span:
            block = block + b"\0" * (span - len(block))
        out += hashlib.sha256(block).digest()
    padded = max(BKTR_BLOCK_SIZE,
                 (len(out) + BKTR_BLOCK_SIZE - 1) // BKTR_BLOCK_SIZE * BKTR_BLOCK_SIZE)
    out += b"\0" * (padded - len(out))
    return bytes(out)


def build_ivfc(image_path: str, l5_block_log2: int = BKTR_BLOCK_LOG2
               ) -> tuple[list[bytes | None], list[int], list[int], bytes]:
    """Build a RomFS IVFC container from a raw RomFS image.

    Returns ``(levels, offsets, sizes, master_hash)``; ``levels[5]`` stays
    ``None`` because the image itself remains on disk.  Verified byte-for-byte
    against the IVFC levels of the base NCA.
    """
    sizes = [0] * 6
    sizes[5] = os.path.getsize(image_path)
    levels: list[bytes | None] = [None] * 6

    with open(image_path, "rb") as fh:
        def read_file(off: int, length: int) -> bytes:
            fh.seek(off)
            return fh.read(length)

        blob = _hash_level(read_file, sizes[5], l5_block_log2)
    levels[4] = blob
    sizes[4] = len(blob)

    for level in (3, 2, 1, 0):
        source = levels[level + 1] or b""

        def read_data(off: int, length: int, data: bytes = source) -> bytes:
            return data[off:off + length]

        blob = _hash_level(read_data, sizes[level + 1])
        levels[level] = blob
        sizes[level] = len(blob)

    offsets = [0] * 6
    for level in range(1, 6):
        offsets[level] = offsets[level - 1] + sizes[level - 1]
    master_hash = hashlib.sha256(levels[0] or b"").digest()
    return levels, offsets, sizes, master_hash


def build_ivfc_header(offsets: list[int], sizes: list[int],
                      master_hash: bytes, num_levels: int = 7,
                      l5_block_log2: int = BKTR_BLOCK_LOG2) -> bytes:
    header = bytearray(0xE0)
    struct.pack_into("<IIII", header, 0x00, MAGIC_IVFC, 0x20000, 0x20, num_levels)
    for level in range(6):
        # Level 5's block size is how coarsely its data is hashed into level 4,
        # so a larger value shrinks the (re-shipped) hash table proportionally.
        log2 = l5_block_log2 if level == 5 else BKTR_BLOCK_LOG2
        struct.pack_into("<QQII", header, 0x10 + level * 0x18,
                         offsets[level], sizes[level], log2, 0)
    header[0xC0:0xE0] = master_hash
    return bytes(header)


# --------------------------------------------------------------------------
# delta regions
# --------------------------------------------------------------------------

def build_regions(virtual_size: int, mapped: list[tuple[int, int, int]]) -> list[dict]:
    """Partition ``[0, virtual_size)`` into base-mapped and patch regions.

    ``mapped`` holds ``(start, end, base_source)`` intervals (sorted, disjoint);
    everything between them has to be stored in the patch data.
    """
    regions: list[dict] = []
    cursor = 0
    for start, end, source in sorted(mapped):
        if start > cursor:
            regions.append({"start": cursor, "end": start, "patch": True})
        delta = start - source
        if (regions and regions[-1]["patch"] is False
                and regions[-1]["end"] == start and regions[-1]["delta"] == delta):
            regions[-1]["end"] = end
        else:
            regions.append({"start": start, "end": end, "patch": False,
                            "delta": delta, "source": source})
        cursor = max(cursor, end)
    if cursor < virtual_size:
        regions.append({"start": cursor, "end": virtual_size, "patch": True})
    return regions


def write_patch_data(regions: list[dict], head: bytes, image_path: str,
                     l5_offset: int, patch_path: str) -> int:
    """Materialise every patch region; returns the patch data length."""
    head_len = len(head)
    position = 0
    with open(patch_path, "wb") as patch:
        for region in regions:
            if not region["patch"]:
                continue
            position = (position + 0xF) & ~0xF          # keep physical 16-byte aligned
            padding = position - patch.tell()
            if padding > 0:
                patch.write(b"\0" * padding)
            region["offset"] = position
            start, end = region["start"], region["end"]
            if end <= head_len:
                patch.write(head[start:end])
            elif start >= head_len:
                patch.write(read_file_range(image_path, start - l5_offset, end - start))
            else:
                patch.write(head[start:])
                patch.write(read_file_range(image_path, 0, end - head_len))
            position += end - start
    return position


def build_bktr_section(regions: list[dict], virtual_size: int,
                       patch_path: str | None, patch_len: int) -> tuple[bytes, int, int]:
    """Assemble the BKTR section; returns ``(section, reloc_offset, subsec_offset)``.

    Patch data goes first, then the relocation tables, then the subsection
    tables (which end the media block).  Nothing but real buckets may sit inside
    either table region, because readers size those regions from the headers.
    """
    buckets: list[list[dict]] = []
    for region in regions:
        if not buckets or len(buckets[-1]) >= RELOC_BUCKET_ENTRIES - 1:
            buckets.append([])
        buckets[-1].append(region)
    buckets = buckets or [[]]

    reloc_tables = 0x4000 * (1 + len(buckets))
    # The NCA section table counts media units of 0x200, so the whole section
    # (patch + tables) has to be 0x200 aligned.
    patch_len = (patch_len + 0x1FF) & ~0x1FF
    reloc_offset = patch_len
    subsec_offset = reloc_offset + reloc_tables
    section_size = subsec_offset + 0x8000

    # Relocation block header: bucket count, virtual size, bucket offsets.
    # bucket_virtual_offsets[0] is unused; readers scan indices 1..num_buckets-1.
    reloc_header = bytearray(0x4000)
    struct.pack_into("<IIQ", reloc_header, 0x00, 0, len(buckets), virtual_size)
    for i, bucket in enumerate(buckets):
        if i:
            struct.pack_into("<Q", reloc_header, 0x10 + i * 8, bucket[0]["start"])

    reloc_body = bytearray()
    for i, bucket in enumerate(buckets):
        blob = bytearray(0x4000)
        end = buckets[i + 1][0]["start"] if i + 1 < len(buckets) else virtual_size
        struct.pack_into(RELOC_BUCKET_FMT, blob, 0, 0, len(bucket), end)
        for j, region in enumerate(bucket):
            if region["patch"]:
                source = region["offset"]      # patch data starts at section offset 0
                is_patch = 1
            else:
                source = region["source"]
                is_patch = 0
            struct.pack_into(RELOC_ENTRY_FMT, blob, 0x10 + j * RELOC_ENTRY_SIZE,
                             region["start"], source, is_patch)
        reloc_body += blob

    # Subsection header + one bucket.  ctr_val 0 everywhere plus a zero
    # section_ctr makes the BKTR crypto degenerate to the plain section CTR.
    subsec_header = bytearray(0x4000)
    struct.pack_into("<IIQ", subsec_header, 0x00, 0, 1, subsec_offset)
    subsec_bucket = bytearray(0x4000)
    struct.pack_into(SUBSEC_BUCKET_FMT, subsec_bucket, 0, 0, 1, subsec_offset)
    struct.pack_into(SUBSEC_ENTRY_FMT, subsec_bucket, 0x10, 0, 0, 0)

    section = bytearray()
    if patch_path is not None:
        section += Path(patch_path).read_bytes()
    section += b"\0" * (reloc_offset - len(section))
    section += bytes(reloc_header) + bytes(reloc_body) + bytes(subsec_header) + bytes(subsec_bucket)
    if len(section) != section_size:
        die(f"BKTR 段长度不一致: 0x{len(section):X} != 0x{section_size:X}")
    return bytes(section), reloc_offset, subsec_offset


def bktr_fs_header(ivfc: bytes, regions: list[dict], reloc_offset: int,
                   subsec_offset: int, section_size: int) -> bytes:
    """FS header (0x200) carrying the IVFC and both BKTR table headers."""
    header = bytearray(0x200)
    header[0x00:0x08] = bytes((2, 0, 0, 3, 4, 0, 0, 0))   # version/part/fs/crypt
    header[0x08:0x08 + 0xE0] = ivfc
    reloc_at = 0x08 + 0xE0 + 0x18
    struct.pack_into(BKTR_HEADER_FMT, header, reloc_at,
                     reloc_offset, subsec_offset - reloc_offset,
                     MAGIC_BKTR, 1, len(regions), 0)
    struct.pack_into(BKTR_HEADER_FMT, header, reloc_at + 0x20,
                     subsec_offset, section_size - subsec_offset, MAGIC_BKTR, 1, 1, 0)
    return bytes(header)


def rewrite_program_nca(path: str, header_key: bytes, keys: dict[str, bytes],
                        section: bytes, fs_header: bytes) -> None:
    raw = Path(path).read_bytes()
    header = bytearray(decrypt_header(raw[:0xc00], header_key))
    start, old_end = section_range(header, 1)
    if old_end == 0:
        _, start = section_range(header, 0)
    if start == 0:
        die("hacbrewpack 的 Program NCA 没有可用于 RomFS 的段")
    key = section_key(header, keys)
    encrypted = aes_ctr(key, section_ctr(start), section)
    new_raw = bytearray(raw[:start * 0x200])
    new_raw.extend(encrypted)
    header[0x600:0x800] = fs_header
    struct.pack_into("<II", header, 0x250, start, start + len(section) // 0x200)
    struct.pack_into("<Q", header, 0x208, start * 0x200 + len(section))
    header[0x740:0x748] = b"\0" * 8        # section_ctr -> plain CTR
    header[0x2A0:0x2C0] = hashlib.sha256(bytes(header[0x600:0x800])).digest()
    new_raw[:0xc00] = encrypt_header(bytes(header), header_key)
    Path(path).write_bytes(new_raw)


def patch_program_nca_zero(path: str, base_header: bytes,
                           keys: dict[str, bytes], header_key: bytes) -> None:
    """Zero-change overlay: map the whole base RomFS back to the base NCA."""
    base_start, base_end = section_range(base_header, 1)
    virtual_size = (base_end - base_start) * 0x200
    region = {"start": 0, "end": virtual_size, "patch": False,
              "delta": 0, "source": 0}
    section, reloc_offset, subsec_offset = build_bktr_section([region], virtual_size, None, 0)
    ivfc = base_header[0x608:0x6E8]
    fs_header = bktr_fs_header(ivfc, [region], reloc_offset, subsec_offset, len(section))
    rewrite_program_nca(path, header_key, keys, section, fs_header)
    print(f"Program BKTR: base_romfs=0x{virtual_size:X} overlay=0x{len(section):X} (零改动)")


def compare_tree(source_dir: str, extracted_dir: str) -> tuple[list[str], list[str]]:
    """``(mismatched, extra)`` relative paths between a source tree and a dump."""
    def collect(root_dir: str) -> dict[str, Path]:
        out: dict[str, Path] = {}
        for root, _dirs, files in os.walk(root_dir):
            for name in files:
                path = Path(root) / name
                out[str(path.relative_to(root_dir))] = path
        return out

    want, have = collect(source_dir), collect(extracted_dir)
    bad = []
    for rel, path in sorted(want.items()):
        other = have.get(rel)
        size = path.stat().st_size
        if (other is None or other.stat().st_size != size
                or hash_file_range(str(path), 0, size)
                != hash_file_range(str(other), 0, other.stat().st_size)):
            bad.append(rel)
    return bad, sorted(set(have) - set(want))


def verify_romfs_image(hactool: str, image_path: str, source_dir: str, workdir: Path) -> None:
    """Re-read the assembled RomFS with hactool and byte-compare every file."""
    out = workdir / "verify-romfs"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    proc = subprocess.run([hactool, "-t", "romfs", "-x", "--romfsdir=" + str(out),
                           image_path], capture_output=True, text=True)
    if proc.returncode:
        sys.stderr.write(proc.stdout + proc.stderr)
        die("hactool 读不了拼接后的 RomFS 镜像")
    bad, extra = compare_tree(source_dir, str(out))
    if bad or extra:
        die(f"拼接镜像校验失败: 不一致 {bad[:4]} 多余 {extra[:4]}")
    files = sum(len(names) for _root, _dirs, names in os.walk(source_dir))
    print(f"  自检: hactool 从拼接镜像解出 {files} 个文件，逐字节一致", flush=True)


def patch_program_nca_delta(path: str, base_nsp: str, base_member: str, base_header: bytes,
                            keys: dict[str, bytes], header_key: bytes,
                            romfsdir: str, build_romfs: str, workdir: Path,
                            hactool: str | None = None,
                            l5_block_log2: int = BKTR_BLOCK_LOG2) -> None:
    """Delta overlay: only genuinely new bytes are shipped.

    Every unchanged base file is mapped back to the base NCA (``is_patch=0``),
    which is what stock updates do; only new/changed file data plus the IVFC
    hash levels go into the patch data.  ``l5_block_log2`` sets how coarsely
    level 5 is hashed into level 4, which is what sizes that hash table.
    """
    base_start, _base_end = section_range(base_header, 1)
    section_off = base_start * 0x200
    key = section_key(base_header, keys)

    def level(field: int, size: bool = False) -> int:
        start = 0x608 + 0x10 + field * 0x18 + (8 if size else 0)
        return int.from_bytes(base_header[start:start + 8], "little")

    l5_off, l5_size = level(5), level(5, True)
    nca_base = member_data_offset(base_nsp, base_member)

    built = str(workdir / "romfs-built.img")
    print(f"  生成新 RomFS 镜像: {romfsdir}", flush=True)
    subprocess.run([build_romfs, romfsdir, built], check=True)
    if hactool:
        verify_romfs_image(hactool, built, romfsdir, workdir)

    base_image = str(workdir / "romfs-base.img")
    print(f"  取本体 RomFS 镜像 0x{l5_size:X} ...", flush=True)
    with open(base_image, "wb") as out:
        for chunk in iter_section_plaintext(base_nsp, key, section_off + l5_off,
                                            l5_size, nca_base):
            out.write(chunk)

    _base_header, base_files = read_local_romfs(base_image)
    _built_header, built_files = read_local_romfs(built)
    print(f"  文件: 本体 {len(base_files)} 个，更新 {len(built_files)} 个；"
          f"校验未改动文件 ...", flush=True)

    reusable: dict[str, int] = {}
    reused = 0
    for name, (new_off, new_size) in sorted(built_files.items()):
        entry = base_files.get(name)
        if entry is None or entry[1] != new_size or new_size == 0:
            continue
        if hash_file_range(built, new_off, new_size) != hash_file_range(
                base_image, entry[0], new_size):
            continue
        reusable[name] = entry[0]
        reused += new_size

    image = built
    levels, offsets, sizes, master = build_ivfc(image, l5_block_log2)
    virtual_size = offsets[5] + sizes[5]
    if l5_block_log2 != BKTR_BLOCK_LOG2:
        print(f"  L5 哈希块 0x{1 << l5_block_log2:X}（L4 0x{sizes[4]:X}）", flush=True)

    mapped: list[tuple[int, int, int]] = []
    for name, (new_off, new_size) in sorted(built_files.items()):
        if name not in reusable:
            continue
        mapped.append((offsets[5] + new_off, offsets[5] + new_off + new_size,
                       l5_off + reusable[name]))

    regions = build_regions(virtual_size, mapped)
    reffed, patched, split_files = [], [], []
    for name, (off, size) in sorted(built_files.items()):
        v0, v1 = offsets[5] + off, offsets[5] + off + size
        hit = [r for r in regions if r["start"] < v1 and r["end"] > v0]
        if all(r["patch"] for r in hit):
            patched.append(name)
        elif all(not r["patch"] for r in hit):
            reffed.append(name)
        else:
            split_files.append(name)
    print(f"  文件归属: 回指本体 {len(reffed)} 个，补丁 {len(patched)} 个"
          + (f"，跨段 {split_files}" if split_files else ""), flush=True)
    print(f"            回指: {', '.join(reffed) or '-'}", flush=True)
    print(f"            补丁: {', '.join(patched) or '-'}", flush=True)
    if split_files:
        print(f"  警告: 这些文件被切在回指段与补丁段之间: {split_files}", file=sys.stderr)
    below = [r for r in regions if not r["patch"] and r["source"] < l5_off]
    if below:
        print(f"  注意: {len(below)} 段回指本体 IVFC 哈希表区（< 本体 L5 0x{l5_off:X}），"
              f"hactool/LibHac 接受，yuzu 与部分加载器不接受", file=sys.stderr)

    patch_path = str(workdir / "bktr-patch.bin")
    patch_len = write_patch_data(regions, b"".join(level for level in levels[:5] if level),
                                 image, offsets[5], patch_path)
    patch_regions = sum(1 for region in regions if region["patch"])
    print(f"  差分: {len(regions)} 段（回指本体 {len(regions) - patch_regions}，"
          f"新增 {patch_regions}），回指 0x{reused:X}，新增数据 0x{patch_len:X}", flush=True)
    if reused == 0:
        print("  警告: 没有任何文件回指本体，--romfsdir 可能不是本体的完整内容", file=sys.stderr)
    elif patch_len > virtual_size // 2:
        print("  警告: 新增数据超过虚拟镜像一半，差分可能没有生效", file=sys.stderr)

    section, reloc_offset, subsec_offset = build_bktr_section(regions, virtual_size,
                                                              patch_path, patch_len)
    fs_header = bktr_fs_header(build_ivfc_header(offsets, sizes, master,
                                                  l5_block_log2=l5_block_log2), regions,
                               reloc_offset, subsec_offset, len(section))
    rewrite_program_nca(path, header_key, keys, section, fs_header)
    print(f"Program BKTR: virtual=0x{virtual_size:X} overlay=0x{len(section):X} "
          f"patch=0x{patch_len:X}")

# --------------------------------------------------------------------------
# Patch CNMT
# --------------------------------------------------------------------------

def parse_base_cnmt(data: bytes) -> dict:
    tid, ver, meta_type, _platform, ext_size, entry_count, _ = struct.unpack_from(
        "<QIBBHHH", data, 0)
    contents = []
    off = 0x20 + ext_size
    for _ in range(entry_count):
        contents.append((data[off + 0x20:off + 0x30],
                         int.from_bytes(data[off + 0x30:off + 0x36], "little"),
                         data[off + 0x36]))
        off += 0x38
    required_system_version = 0
    if meta_type == META_TYPE_APPLICATION:
        _, required_system_version, _ = struct.unpack_from("<QII", data, 0x20)
    return {"id": tid, "version": ver, "type": meta_type,
            "contents": contents, "digest": data[-0x20:],
            "required_system_version": required_system_version}


def build_patch_extended_data(base: dict, base_meta_name: str,
                              base_meta_size: int) -> bytes:
    meta_id = bytes.fromhex(Path(base_meta_name).name.split(".")[0])
    infos = list(base["contents"]) + [(meta_id, base_meta_size, 0)]
    out = struct.pack("<IIIIIII", 1, 0, 0, 0, len(infos), 0, 0)
    out += struct.pack("<QIB3x", base["id"], base["version"], base["type"])
    out += base["digest"]
    out += struct.pack("<H6x", len(infos))
    for content_id, size, content_type in infos:
        out += content_id + size.to_bytes(6, "little") + bytes([content_type, 0])
    return out


def content_record(path: str, content_type: int) -> bytes:
    blob = Path(path).read_bytes()
    digest = hashlib.sha256(blob).digest()
    return digest + digest[:16] + len(blob).to_bytes(6, "little") + bytes([content_type, 0])


def build_patch_cnmt(update_id: int, base_id: int, version: int,
                     program: str, control: str, extended_data: bytes,
                     required_system_version: int) -> bytes:
    records = content_record(program, CT_PROGRAM) + content_record(control, CT_CONTROL)
    ext = struct.pack(PATCH_EXT_FMT, base_id, required_system_version,
                      len(extended_data), 0)
    header = struct.pack(CNMT_HEADER_FMT, update_id, version, META_TYPE_PATCH, 0,
                         len(ext), 2, 0, 0, 0, 0, 0, 0, 0)
    return header + ext + records + extended_data + b"\0" * 0x20


def build_pfs0(entries: list[tuple[str, bytes]]) -> bytes:
    strings = b""
    offsets: dict[str, int] = {}
    for name, _ in entries:
        if name not in offsets:
            offsets[name] = len(strings)
            strings += name.encode() + b"\0"
    table = bytearray()
    body = bytearray()
    for name, blob in entries:
        off = len(body)
        body += blob
        body += b"\0" * ((-len(body)) % 0x20)
        table += struct.pack("<QQI4x", off, len(blob), offsets[name])
    return b"PFS0" + struct.pack("<III", len(entries), len(strings), 0) + table + strings + body


def patch_meta_nca(path: str, cnmt: bytes, update_id: int,
                   keys: dict[str, bytes], header_key: bytes) -> None:
    raw = Path(path).read_bytes()
    header = bytearray(decrypt_header(raw[:0xc00], header_key))
    start, end = section_range(header, 0)
    section_size = (end - start) * 0x200
    pfs = build_pfs0([(f"Patch_{update_id:016x}.cnmt", cnmt)])
    if len(pfs) > section_size - 0x200:
        die(f"Patch CNMT 放不进 Meta NCA: pfs0=0x{len(pfs):X}")
    key = section_key(header, keys)
    plain_section = bytearray(section_size)
    plain_section[0x200:0x200 + len(pfs)] = pfs
    plain_section[0:32] = hashlib.sha256(pfs).digest()
    header[0x210:0x218] = update_id.to_bytes(8, "little")
    header[0x408:0x428] = hashlib.sha256(plain_section[0:32]).digest()
    struct.pack_into("<Q", header, 0x448, len(pfs))
    header[0x280:0x2A0] = hashlib.sha256(header[0x400:0x600]).digest()
    encrypted = aes_ctr(key, section_ctr(start), bytes(plain_section))
    new_raw = bytearray(raw)
    new_raw[start * 0x200:end * 0x200] = encrypted
    new_raw[:0xc00] = encrypt_header(bytes(header), header_key)
    Path(path).write_bytes(new_raw)


def extract_nca(hactool: str, keyset: str, nca: str, outdir: str) -> None:
    os.makedirs(outdir, exist_ok=True)
    proc = subprocess.run([hactool, "-k", keyset, "--disablekeywarns", "-t", "nca", "-x",
                           "--section0dir=" + outdir, nca],
                          capture_output=True, text=True)
    if proc.returncode:
        sys.stderr.write(proc.stdout + proc.stderr)
        die(f"hactool 解包失败: {nca}")


def run_hacbrewpack(hacbrewpack: str, keyset: str, base_id: str,
                    exefs: str, control: str, outdir: str) -> dict[str, str]:
    work = Path(outdir)
    work.mkdir(parents=True, exist_ok=True)
    shutil.copytree(exefs, work / "exefs")
    shutil.copytree(control, work / "control")
    (work / "romfs").mkdir()
    cmd = [hacbrewpack, "--keyset", keyset, "--titleid", base_id,
           "--exefsdir", str(work / "exefs"), "--romfsdir", str(work / "romfs"),
           "--nologo", "--keepncadir"]
    proc = subprocess.run(cmd, cwd=work, capture_output=True, text=True)
    if proc.returncode:
        sys.stderr.write(proc.stdout + proc.stderr)
        die("hacbrewpack 生成 update 内容 NCA 失败")
    nca_dir = work / "hacbrewpack_nca"
    ncas = list(nca_dir.glob("*.nca"))
    if not ncas:
        die("hacbrewpack 没有生成 NCA")
    result: dict[str, str] = {}
    for nca in ncas:
        info = subprocess.run(["/opt/devkitpro/tools/bin/hactool", "-k", keyset, "-i", str(nca)],
                              capture_output=True, text=True).stdout
        if "Content Type:                       Program" in info:
            result["program"] = str(nca)
        elif "Content Type:                       Control" in info:
            result["control"] = str(nca)
        elif "Content Type:                       Meta" in info:
            result["meta"] = str(nca)
    if set(result) != {"program", "control", "meta"}:
        die(f"hacbrewpack NCA 类型不完整: {result}")
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-nsp", required=True)
    ap.add_argument("--exefsdir", required=True)
    ap.add_argument("--base-titleid", required=True)
    ap.add_argument("--update-titleid", required=True)
    ap.add_argument("--version", default="1.0.1")
    ap.add_argument("--keyset", default=os.path.expanduser("~/.switch/prod.keys"))
    ap.add_argument("--hactool", default="/opt/devkitpro/tools/bin/hactool")
    ap.add_argument("--hacbrewpack", default=os.path.expanduser("~/bin/hacbrewpack"))
    ap.add_argument("--build-romfs", default="/opt/devkitpro/tools/bin/build_romfs")
    ap.add_argument("--romfsdir", default=None,
                    help="新的完整 RomFS 目录；给了它才生成 RomFS 差分")
    ap.add_argument("--verify-romfs", action="store_true",
                    help="打包前用 hactool 重读 RomFS 镜像并逐字节比对源目录")
    ap.add_argument("--l5-block-log2", type=int, default=BKTR_BLOCK_LOG2,
                    help="Level 5 哈希块大小的 log2（默认 14=0x4000；越大哈希表越小）")
    ap.add_argument("--out", required=True)
    ap.add_argument("--workdir", default=None)
    args = ap.parse_args()

    base_nsp = os.path.abspath(args.base_nsp)
    exefs = os.path.abspath(args.exefsdir)
    keyset = os.path.abspath(args.keyset)
    if not os.path.isfile(base_nsp) or not os.path.isdir(exefs) or not os.path.isfile(keyset):
        die("update NSP 输入不完整")
    if args.romfsdir and not os.path.isdir(args.romfsdir):
        die(f"找不到 RomFS 目录: {args.romfsdir}")
    tmp = Path(args.workdir or tempfile.mkdtemp(prefix="kawa2-update-"))
    tmp.mkdir(parents=True, exist_ok=True)
    keys = parse_keys(keyset)
    header_key = keys.get("header_key")
    if header_key is None:
        die("prod.keys 缺少 header_key")
    base_id = int(args.base_titleid, 16)
    update_id = int(args.update_titleid, 16)
    version = parse_version(args.version)

    members = pfs0_members(base_nsp)
    meta_name = next((n for n, _, _ in members if n.endswith(".cnmt.nca")), None)
    non_meta = [(n, size) for n, _, size in members
                if n.endswith(".nca") and not n.endswith(".cnmt.nca")]
    if not meta_name or len(non_meta) != 2:
        die(f"本体 NSP 成员不符合预期: {[n for n, _, _ in members]}")
    (control_name, _control_size), (program_name, _program_size) = sorted(
        non_meta, key=lambda x: x[1])
    base_meta = tmp / meta_name
    base_meta.write_bytes(read_member(base_nsp, meta_name))
    base_meta_dir = tmp / "base-meta"
    extract_nca(args.hactool, keyset, str(base_meta), str(base_meta_dir))
    cnmt_files = list(base_meta_dir.glob("*.cnmt"))
    if len(cnmt_files) != 1:
        die("本体 Meta NCA 没有解出 CNMT")
    base_cnmt = parse_base_cnmt(cnmt_files[0].read_bytes())

    base_ctrl_nca = tmp / control_name
    base_ctrl_nca.write_bytes(read_member(base_nsp, control_name))
    base_ctrl_dir = tmp / "base-control"
    extract_nca(args.hactool, keyset, str(base_ctrl_nca), str(base_ctrl_dir))
    nacp = base_ctrl_dir / "control.nacp"
    if not nacp.is_file():
        die("本体 Control NCA 没有 control.nacp")
    nacp_data = bytearray(nacp.read_bytes())
    nacp_data[0x3060:0x3070] = version_display(args.version, version).ljust(0x10, b"\0")
    nacp.write_bytes(nacp_data)
    if not any(base_ctrl_dir.glob("icon_*.dat")):
        die("本体 Control NCA 没有图标")

    generated = run_hacbrewpack(args.hacbrewpack, keyset, args.base_titleid,
                                exefs, str(base_ctrl_dir), str(tmp / "brew"))
    base_header = decrypt_header(read_member(base_nsp, program_name, 0xC00), header_key)
    if args.romfsdir:
        patch_program_nca_delta(generated["program"], base_nsp, program_name, base_header,
                                keys, header_key, os.path.abspath(args.romfsdir),
                                args.build_romfs, tmp,
                                args.hactool if args.verify_romfs else None,
                                args.l5_block_log2)
    else:
        patch_program_nca_zero(generated["program"], base_header, keys, header_key)
    extended = build_patch_extended_data(
        base_cnmt, meta_name,
        next(size for name, _, size in members if name == meta_name))
    patch_cnmt = build_patch_cnmt(update_id, base_id, version,
                                  generated["program"], generated["control"], extended,
                                  base_cnmt["required_system_version"])
    patch_meta_nca(generated["meta"], patch_cnmt, update_id, keys, header_key)

    blobs = []
    for path in (generated["meta"], generated["program"], generated["control"]):
        digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()[:32]
        renamed = tmp / f"{digest}.nca"
        shutil.copyfile(path, renamed)
        blobs.append((renamed.name, renamed.read_bytes()))
    nsp = build_pfs0(blobs)
    out = Path(args.out).absolute()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(nsp)
    print(f"Patch CNMT: type=0x81 base=0x{base_id:016X} update=0x{update_id:016X}")
    print(f"写出 NSP: {out} ({len(nsp)} bytes)")
    print(f"sha256: {hashlib.sha256(nsp).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
