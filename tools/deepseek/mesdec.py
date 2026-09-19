#!/usr/bin/env python3
"""Decode an AI6WIN MES/LIB module into a readable instruction text, matching
the code offsets, operand byte-widths and interpretation of runtime/vm.c
(validated because vm.c drives start.mes on the documented bootstrap pause).

Operand widths used (same rules as kvm_add_module):
  opcode in {0x0a,0x0b,0x33}: trailing NUL-terminated string literal.
  opcode in {0x14,0x15,0x16,0x19,0x1a,0x32}: one 4-byte big-endian operand.
  everything else: no inline operand (operands live on the variant stack).
Intended for local reverse-engineering only; never shipped in the runtime.
"""
import sys
from pathlib import Path

STR_OPS = {0x0a, 0x0b, 0x33}
WORD_OPS = {0x14, 0x15, 0x16, 0x19, 0x1a, 0x32}

def signed32(b):
    v = int.from_bytes(b, "big", signed=False)
    return v if v < 0x80000000 else v - 0x100000000


def decode(data: bytes):
    # MES header: first u32 = message count (LE), then one u32/msg offset (LE),
    # then the bytecode starts. Path uses the message table size like the runtime.
    import struct
    if len(data) < 4:
        return []
    nm = struct.unpack("<I", data[:4])[0]
    base = 4 + nm * 4
    code = data[base:]
    p = 0
    n = len(code)
    out = []
    while p < n:
        st = p
        op = code[p]
        p += 1
        note = ""
        if op in STR_OPS:
            q = code.find(b"\0", p)
            if q == -1:
                q = n
            raw = code[p:q]
            p = q + 1
            try:
                note = repr(raw.decode("cp932"))
            except Exception:
                note = " " + repr(raw)
        elif op in WORD_OPS:
            if p + 4 > n:
                note = " <TRUNC>"
            else:
                note = " " + str(signed32(code[p:p + 4]))
                p += 4
        out.append((st, op, note))
    return out


def fmt(data):
    for st, op, note in decode(data):
        print(f"{st:05x} {op:02x}{note}")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "local/deepseek/start.mes"
    fmt(Path(path).read_bytes())
