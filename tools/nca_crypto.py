"""Small dependency-free AES helpers used by the update packer.

The update builder only needs AES-128 CTR for NCA sections and AES-128-XTS for
the encrypted NCA header.  Keeping the implementation here avoids depending
on a particular OpenSSL build (OpenSSL 3 no longer exposes XTS through
``openssl enc`` on macOS).
"""

from __future__ import annotations


SBOX = (
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16,
)
INV_SBOX = tuple(SBOX.index(i) for i in range(256))
RCON = (0, 1, 2, 4, 8, 16, 32, 64, 128, 27, 54)


def _xtime(a: int) -> int:
    return ((a << 1) ^ (0x11B if a & 0x80 else 0)) & 0xFF


def _mul(a: int, b: int) -> int:
    out = 0
    while b:
        if b & 1:
            out ^= a
        a = _xtime(a)
        b >>= 1
    return out


def _expand(key: bytes) -> list[bytes]:
    if len(key) != 16:
        raise ValueError("AES-128 requires a 16-byte key")
    words = [bytearray(key[i:i + 4]) for i in range(0, 16, 4)]
    for i in range(4, 44):
        t = words[i - 1][:]
        if i % 4 == 0:
            t = t[1:] + t[:1]
            t = bytearray(SBOX[x] for x in t)
            t[0] ^= RCON[i // 4]
        words.append(bytearray(a ^ b for a, b in zip(words[i - 4], t)))
    return [bytes(b for word in words[i:i + 4] for b in word) for i in range(0, 44, 4)]


def _add_round_key(s: list[int], key: bytes) -> None:
    for i, value in enumerate(key):
        s[i] ^= value


def _sub_bytes(s: list[int], inv: bool = False) -> None:
    box = INV_SBOX if inv else SBOX
    for i in range(16):
        s[i] = box[s[i]]


def _shift_rows(s: list[int], inv: bool = False) -> None:
    old = s[:]
    for row in range(4):
        for col in range(4):
            src = (col + row if not inv else col - row) % 4
            s[row + 4 * col] = old[row + 4 * src]


def _mix_columns(s: list[int], inv: bool = False) -> None:
    for col in range(4):
        i = col * 4
        a, b, c, d = s[i:i + 4]
        if inv:
            s[i:i + 4] = [
                _mul(a, 14) ^ _mul(b, 11) ^ _mul(c, 13) ^ _mul(d, 9),
                _mul(a, 9) ^ _mul(b, 14) ^ _mul(c, 11) ^ _mul(d, 13),
                _mul(a, 13) ^ _mul(b, 9) ^ _mul(c, 14) ^ _mul(d, 11),
                _mul(a, 11) ^ _mul(b, 13) ^ _mul(c, 9) ^ _mul(d, 14),
            ]
        else:
            s[i:i + 4] = [
                _mul(a, 2) ^ _mul(b, 3) ^ c ^ d,
                a ^ _mul(b, 2) ^ _mul(c, 3) ^ d,
                a ^ b ^ _mul(c, 2) ^ _mul(d, 3),
                _mul(a, 3) ^ b ^ c ^ _mul(d, 2),
            ]


def aes_encrypt_block(key: bytes, block: bytes) -> bytes:
    keys = _expand(key)
    s = list(block)
    _add_round_key(s, keys[0])
    for r in range(1, 10):
        _sub_bytes(s)
        _shift_rows(s)
        _mix_columns(s)
        _add_round_key(s, keys[r])
    _sub_bytes(s)
    _shift_rows(s)
    _add_round_key(s, keys[10])
    return bytes(s)


def aes_decrypt_block(key: bytes, block: bytes) -> bytes:
    keys = _expand(key)
    s = list(block)
    _add_round_key(s, keys[10])
    for r in range(9, 0, -1):
        _shift_rows(s, inv=True)
        _sub_bytes(s, inv=True)
        _add_round_key(s, keys[r])
        _mix_columns(s, inv=True)
    _shift_rows(s, inv=True)
    _sub_bytes(s, inv=True)
    _add_round_key(s, keys[0])
    return bytes(s)


def aes_ctr(key: bytes, counter: bytes, data: bytes) -> bytes:
    if len(counter) != 16:
        raise ValueError("AES CTR counter must be 16 bytes")
    value = int.from_bytes(counter, "big")
    out = bytearray()
    for off in range(0, len(data), 16):
        stream = aes_encrypt_block(key, value.to_bytes(16, "big"))
        block = data[off:off + 16]
        out.extend(a ^ b for a, b in zip(block, stream))
        value = (value + 1) & ((1 << 128) - 1)
    return bytes(out)


def _mul_x(tweak: bytearray) -> None:
    carry = 0
    for i in range(16):
        next_carry = tweak[i] >> 7
        tweak[i] = ((tweak[i] << 1) & 0xFF) | carry
        carry = next_carry
    if carry:
        tweak[0] ^= 0x87


def aes_xts_header(key: bytes, data: bytes, decrypt: bool) -> bytes:
    """AES-128-XTS over NCA header sectors.

    The NCA header's encrypted portion starts at file offset 0x200.  The
    caller passes one 0x200-byte sector and its 1-based sector number.
    """
    raise RuntimeError("use aes_xts_sector")


def aes_xts_sector(key: bytes, data: bytes, sector: int, decrypt: bool) -> bytes:
    if len(key) != 32 or len(data) % 16:
        raise ValueError("XTS requires a 32-byte key and block-aligned data")
    tweak = bytearray(aes_encrypt_block(key[16:], sector.to_bytes(16, "big")))
    out = bytearray()
    for off in range(0, len(data), 16):
        block = bytes(a ^ b for a, b in zip(data[off:off + 16], tweak))
        block = aes_decrypt_block(key[:16], block) if decrypt else aes_encrypt_block(key[:16], block)
        out.extend(a ^ b for a, b in zip(block, tweak))
        _mul_x(tweak)
    return bytes(out)
