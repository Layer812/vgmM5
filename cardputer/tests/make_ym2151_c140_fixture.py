#!/usr/bin/env python3
"""Produce an original, redistributable 1.5s YM2151+C140 VGM for CI.

No game music, copyrighted ROM, extracted soundtrack or commercial VGM.
This deliberately tests actual 0x54/0xD4/0x67/0x61 VGM file parsing instead
of only calling synthesized chip writes. Sound is a synthetic test sequence.
"""
from pathlib import Path
import struct
import sys

def u32(x):
    return struct.pack("<I", x)

def make_file(path: Path):
    rom = bytes(((i * 43 + (i >> 3) * 7 + ((i >> 8) & 7) * 23) & 255)
                for i in range(65536))
    commands = bytearray()
    commands += bytes((0x67, 0x66, 0x8d)) + u32(len(rom) + 8)
    commands += u32(len(rom)) + u32(0) + rom

    def fm(reg, value):
        commands.extend((0x54, reg & 0xff, value & 0xff))
    def c140(ch, reg, value):
        addr = ch * 16 + reg
        commands.extend((0xd4, (addr >> 8) & 0x7f, addr & 0xff, value & 0xff))
    def wait(samples):
        assert 0 <= samples <= 65535
        commands.append(0x61)
        commands.extend(struct.pack("<H", samples))

    for ch in range(8):
        fm(0x20 + ch, 0xc0 | ((ch & 3) << 3) | (ch & 7))
        fm(0x28 + ch, 0x36 + ch * 3)
        fm(0x30 + ch, 0x10 + ch * 5)
        fm(0x38 + ch, 0x33)
        for op in range(4):
            off = op * 8 + ch
            fm(0x40 + off, 0x01 + (op & 3))
            fm(0x60 + off, 10 if op == 3 else 24)
            fm(0x80 + off, 0x1f)
            fm(0xa0 + off, 0x08)
            fm(0xc0 + off, 0x0b)
            fm(0xe0 + off, 0x17)
        fm(0x08, 0x78 | ch)

    for ch in range(20):
        start = 0x0800 + ch * 2200
        end = start + 1300
        c140(ch, 0, 0x50 + (ch * 5) % 100)
        c140(ch, 1, 0x60 + (ch * 7) % 110)
        c140(ch, 2, 0x90 + (ch & 3) * 8)
        c140(ch, 3, 0x45 + (ch & 7) * 7)
        c140(ch, 4, 0)
        c140(ch, 6, (start >> 8) & 255)
        c140(ch, 7, start & 255)
        c140(ch, 8, (end >> 8) & 255)
        c140(ch, 9, end & 255)
        c140(ch, 10, (start >> 8) & 255)
        c140(ch, 11, start & 255)
        c140(ch, 5, 0x90)
    wait(22050)
    for ch in range(8):
        fm(0x28 + ch, 0x40 + ch * 3)
    wait(22050)
    for ch in range(0, 20, 2):
        c140(ch, 5, 0x90)
    wait(22050)
    commands.append(0x66)

    header = bytearray(0x100)
    header[0:4] = b"Vgm "
    header[8:12] = u32(0x171)
    header[0x18:0x1c] = u32(66150)
    header[0x30:0x34] = u32(3579545)
    header[0x34:0x38] = u32(0x100 - 0x34)
    header[0x96] = 0
    header[0xa8:0xac] = u32(21390000)
    data = header + commands
    data[4:8] = u32(len(data) - 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print("Generated original VGM command fixture:", path, len(data), "bytes")

if __name__ == "__main__":
    make_file(Path(sys.argv[1] if len(sys.argv) > 1 else
                   "cardputer/tests/ym2151_c140_original_fixture.vgm"))
