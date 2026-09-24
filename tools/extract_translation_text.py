#!/usr/bin/env python3
"""Extract the real text from the Chinese MES patch.

The patch keeps the AI6WIN bytecode layout and replaces each text literal in
place.  Its translated strings still contain the special traditional glyphs
from uif_config.json; applying that character map produces the final Chinese
text used by the patched Windows font.
"""
import argparse
import json
import struct
from pathlib import Path

from ai6arc import Archive

STRING_OPS = {0x0A, 0x0B, 0x33}
WORD_OPS = {0x14, 0x15, 0x16, 0x19, 0x1A, 0x32}


def fix_typos(text):
    """Correct clear OCR/font-substitution artifacts in the supplied patch."""
    replacements = (
        ("釐釐醫醫", "坑坑洼洼"),
        ("釐醫", "疙瘩"),
        ("膵螂", "蟑螂"),
        ("濶脏", "肮脏"),
        ("齲黑", "漆黑"),
        ("黑齲齲", "黑漆漆"),
        ("鰒瓸", "难看"),
        ("濶、肮脏", "脏、肮脏"),
        ("膵、蟑螂", "蟑、蟑螂"),
        ("寒瓸", "寒酸"),
        ("偵探", "侦探"),
        ("禿头", "秃头"),
        ("鷭野", "姬野"),
        ("大声鱶鱶", "大声喧哗"),
        ("吵吵鱶鱶", "吵吵闹闹"),
        ("瞎鱶鱶", "瞎嚷嚷"),
        ("鱶鱶着", "嚷嚷着"),
        ("鱶鱶什么", "嚷嚷什么"),
        ("叫鱶", "叫喊"),
        ("鱶时", "喊时"),
        ("鱶鱶", "吵闹"),
        ("逹进去", "插进去"),
        ("逹进", "插进"),
        ("逹入", "插入"),
        ("逹到", "插到"),
        ("逹穿", "捅穿"),
        ("逹刀子", "捅刀子"),
        ("逹一刀", "捅上一刀"),
        ("逹刀", "捅刀"),
        ("逹死", "刺死"),
        ("逹开的", "刺开的"),
        ("逹了一次", "插了一次"),
        ("被逹", "被插"),
        ("逹几下", "插几下"),
        ("逹", "插"),
        ("插死你", "刺死你"),
        ("插刀子", "捅刀子"),
        ("插上一刀", "捅上一刀"),
        ("插刀", "捅刀"),
    )
    for source, target in replacements:
        text = text.replace(source, target)
    return text


def read_entry(archive, entry):
    with archive.path.open("rb") as f:
        f.seek(entry.offset)
        packed = f.read(entry.packed)
    if entry.packed == entry.size:
        return packed
    ring = bytearray(4096)
    output = bytearray()
    pos, cursor = 0, 4078
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


def instructions(data):
    count = struct.unpack_from("<I", data, 0)[0]
    pos = 4 + count * 4
    result = []
    while pos < len(data):
        opcode = data[pos]
        pos += 1
        if opcode in STRING_OPS:
            end = data.find(b"\0", pos)
            if end < 0:
                raise ValueError("unterminated string literal")
            raw = data[pos:end]
            pos = end + 1
            try:
                text = raw.decode("cp932")
            except UnicodeDecodeError as exc:
                raise ValueError(f"invalid CP932 string at {pos}") from exc
            result.append((opcode, text))
        elif opcode in WORD_OPS:
            if pos + 4 > len(data):
                raise ValueError("truncated word operand")
            pos += 4
            result.append((opcode, None))
        elif opcode == 0x1B:
            if pos >= len(data):
                raise ValueError("truncated newline operand")
            pos += 1
            result.append((opcode, None))
        else:
            result.append((opcode, None))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("original_archive", type=Path)
    parser.add_argument("patch_archive", type=Path)
    parser.add_argument("config", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    config = json.loads(args.config.read_text(encoding="utf-8"))["character_substitution"]
    mapping = dict(zip(config["source_characters"], config["target_characters"]))
    original = Archive(args.original_archive)
    patch = Archive(args.patch_archive)
    pairs = dict(mapping)
    scripts = strings = changed = 0
    for original_entry in original.entries:
        patch_entry = next(e for e in patch.entries if e.name.lower() == original_entry.name.lower())
        old = instructions(read_entry(original, original_entry))
        new = instructions(read_entry(patch, patch_entry))
        if len(old) != len(new) or any(a[0] != b[0] for a, b in zip(old, new)):
            raise ValueError(f"instruction layout mismatch: {original_entry.name}")
        scripts += 1
        for (old_op, source), (new_op, patched) in zip(old, new):
            if old_op not in STRING_OPS:
                continue
            strings += 1
            translated = fix_typos("".join(mapping.get(char, char) for char in patched))
            if source != translated:
                pairs[source] = translated
                changed += 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# UTF-8 external full-text table extracted from the Chinese mes.arc patch.\n")
        f.write("# Format: original text TAB translated text. Longest matches win.\n")
        for source, translated in sorted(pairs.items(), key=lambda item: (-len(item[0]), item[0])):
            f.write(f"{source}\t{translated}\n")
    print(f"scripts={scripts} strings={strings} changed={changed} entries={len(pairs)} output={args.output}")


if __name__ == "__main__":
    main()
