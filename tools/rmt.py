"""Decode RMT to top-down BGRA bytes; no image is displayed or exported.

Format reference: morkt/GARbro, ArcFormats/elf/ImageRMT.cs (MIT).
"""
import struct
from ai6arc import lzss


def decode(data):
    if len(data) < 20 or data[:4] != b'RMT ':
        raise ValueError('invalid RMT header')
    x, y, width, height = struct.unpack_from('<iiII', data, 4)
    if not width or not height or width * height > 16777216:
        raise ValueError('invalid RMT dimensions')
    stride = width * 4
    pixels = bytearray(lzss(data[20:], stride * height))
    for i in range(4, stride):
        pixels[i] = (pixels[i] + pixels[i - 4]) & 255
    for i in range(stride, len(pixels)):
        pixels[i] = (pixels[i] + pixels[i - stride]) & 255
    flipped = b''.join(pixels[i:i + stride] for i in range(len(pixels) - stride, -1, -stride))
    return (x, y, width, height), flipped
