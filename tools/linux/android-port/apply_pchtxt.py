"""Apply exefs mods (.pchtxt patch files, as Switch emulators take them) to an NSO, writing a patched NSO.

    apply_pchtxt.py <exefs/main> <out NSO> <file.pchtxt | folder> ...

A recompiled game can't take an exefs mod at run time - its code is C made from the NSO ahead of time. So the patch
is made to the NSO itself, before the generator reads it (tools/gen.py: it lifts the patched instructions as an
emulator would run them) and for the runtime's loader (runtime/loader.c: the patched data). Build with it:
ZA_MODS=<folder> build_za.sh.

The .pchtxt format: "@nsobid-<build id>" (checked against the NSO's), "@enabled" ... "@stop" blocks of
"<offset> <hex bytes>" lines (offsets into the module's image; "@flag offset_shift 0x100" is what an IPS converter adds
for the NSO header - the offsets here are the image's own), "//" comments. A folder is searched for *.pchtxt named
after the game version (2.0.2.pchtxt) or holding the NSO's build id.

The output is uncompressed (no LZ4, no segment hash checks), with the same build id: the generator and the game code
library's id check see the same game version. Bytes past the data segment's end, in .bss (a mod's own variables), make
the data segment longer (and .bss shorter), so they are loaded instead of zeroed.
"""
import os
import struct
import sys

import lz4.block


def read_nso(path):
    raw = open(path, 'rb').read()
    assert raw[:4] == b'NSO0', f'{path}: not an NSO'
    flags = struct.unpack_from('<I', raw, 0xC)[0]
    segs = []
    for i in range(3):
        file_off, mem_off, dec_size = struct.unpack_from('<III', raw, 0x10 + i * 0x10)
        comp_size = struct.unpack_from('<I', raw, 0x60 + i * 4)[0]
        data = raw[file_off:file_off + comp_size]
        if flags & (1 << i):
            data = lz4.block.decompress(data, uncompressed_size=dec_size)
        segs.append([mem_off, bytearray(data)])
    bss = struct.unpack_from('<I', raw, 0x3C)[0]
    build_id = raw[0x40:0x60].rstrip(b'\0').hex().upper()
    return raw, segs, bss, build_id


def parse_pchtxt(path, build_id):
    """[(offset, bytes, note)] of the enabled blocks; None when the file is for another build."""
    out, enabled = [], False
    for line in open(path, encoding='utf-8', errors='replace'):
        t = line.split('//')[0].strip()
        if not t:
            continue
        if t.startswith('@nsobid-'):
            if not build_id.startswith(t[len('@nsobid-'):].strip().upper()[:16]):
                return None
            continue
        if t == '@enabled':
            enabled = True
            continue
        if t == '@stop':
            enabled = False
            continue
        if t.startswith('@') or not enabled:
            continue
        parts = t.split()
        if len(parts) < 2:
            continue
        try:
            out.append((int(parts[0], 16), bytes.fromhex(''.join(parts[1:])), os.path.basename(path)))
        except ValueError:
            sys.exit(f'{path}: cannot read "{t}"')
    return out


def find_files(arg, build_id, version_name):
    if os.path.isfile(arg):
        return [arg]
    found = []
    for root, _, files in os.walk(arg):
        for f in sorted(files):
            if not f.endswith('.pchtxt'):
                continue
            p = os.path.join(root, f)
            head = open(p, encoding='utf-8', errors='replace').read(400).upper()
            if f == version_name or build_id[:16] in head:
                found.append(p)
    return found


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, dst, inputs = sys.argv[1], sys.argv[2], sys.argv[3:]
    raw, segs, bss, build_id = read_nso(src)
    version_name = os.environ.get('ZA_VERSION_FILE', '2.0.2.pchtxt')
    patches = []
    for a in inputs:
        for f in find_files(a, build_id, version_name):
            p = parse_pchtxt(f, build_id)
            if p is None:
                print(f'  {f}: for another build, left out')
                continue
            print(f'  {f}: {len(p)} patch line(s)')
            patches += p
    if not patches:
        sys.exit('no patch for this build (%s)' % build_id[:16])
    data_off, data = segs[2]
    for off, b, note in patches:
        seg = next((s for s in segs if s[0] <= off < s[0] + len(s[1])), None)
        if seg is None and data_off + len(data) <= off < data_off + len(data) + bss:
            grow = off + len(b) - (data_off + len(data))  # (.bss: the data segment takes it)
            data.extend(b'\0' * grow)
            bss -= grow
            seg = segs[2]
        if seg is None or off + len(b) > seg[0] + len(seg[1]):
            sys.exit(f'{note}: offset {off:#x} is outside the module')
        seg[1][off - seg[0]:off - seg[0] + len(b)] = b
    # the NSO again, uncompressed: header (0x100 bytes) then the three segments
    hdr = bytearray(raw[:0x100])
    flags = struct.unpack_from('<I', hdr, 0xC)[0] & ~0x3F  # (no compression, no hash checks)
    struct.pack_into('<I', hdr, 0xC, flags)
    body, pos = bytearray(), 0x100
    for i, (mem_off, d) in enumerate(segs):
        struct.pack_into('<III', hdr, 0x10 + i * 0x10, pos, mem_off, len(d))
        struct.pack_into('<I', hdr, 0x60 + i * 4, len(d))
        body += d
        pos += len(d)
    struct.pack_into('<I', hdr, 0x3C, bss)
    with open(dst, 'wb') as f:
        f.write(hdr + body)
    print(f'{dst}: {len(patches)} patch line(s) applied, build {build_id[:16]}, .bss {bss:#x}')


if __name__ == '__main__':
    main()
