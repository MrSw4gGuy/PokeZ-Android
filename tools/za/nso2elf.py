"""Z-A's exefs/main (an NSO: LZ4 segments) as an ELF Ghidra opens without a Switch loader:
    python tools/za/nso2elf.py <exefs/main> <out.elf>
Three PT_LOAD segments at 0x7000000000, the address the generator and every note use (text RX, rodata R, data + bss
RW), entry at the text start, and the RELATIVE relocations applied, so vtables and function pointers point at real
addresses. No section headers: Ghidra builds its memory map from the segments. The output holds the game's code -
the player's own dump, never committed or shared."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from nso import Nso, R_AARCH64_RELATIVE  # noqa: E402

BASE = 0x7000000000


def main(src, out):
    n = Nso(src)
    img = n.image
    for off, typ, _sym, add in n.relocs:
        if typ == R_AARCH64_RELATIVE and off + 8 <= len(img):
            struct.pack_into('<Q', img, off, BASE + add)
    segs = [(n.text, 5), (n.rodata, 4), (n.data, 6)]  # (PF_R 4, PF_W 2, PF_X 1)
    eh = b'\x7fELF' + bytes([2, 1, 1, 0]) + bytes(8)  # (64-bit, little-endian, version 1)
    eh += struct.pack('<HHIQQQIHHHHHH', 2, 183, 1, BASE, 64, 0, 0, 64, 56, len(segs), 0, 0, 0)  # (EXEC, AArch64)
    ph = b''
    for i, ((off, size), flags) in enumerate(segs):
        mem = size if i < 2 else n.size - off  # (data: through the bss)
        ph += struct.pack('<IIQQQQQQ', 1, flags, 0x1000 + off, BASE + off, BASE + off, size, mem, 0x1000)
    with open(out, 'wb') as f:
        f.write((eh + ph).ljust(0x1000, b'\0'))
        f.write(bytes(img[:n.data[0] + n.data[1]]))
    print(f'{out}: build {n.build_id[:16]}, {len(n.relocs)} relocations, base 0x{BASE:x}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
