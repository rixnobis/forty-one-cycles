#!/usr/bin/env python3
"""Writes the disc's system-area licence file (LBA 0-11, 12 x 2336-byte
sectors of 8-byte subheader + 2048 data + 280).

Nothing here comes from a Sony disc: sector 4 carries our own text and
sectors 5-11 our own logo, an extruded "41" as a TMD of flat triangles.
The BIOS shows that logo at boot. NTSC US BIOSes accept any logo; PAL
v4.0E+ and Japanese BIOSes compare it against their own copy and refuse.

    python3 tools/gen_licence.py out/licence.dat
"""

import struct
import sys

GLYPH = [  # "41", one character per 1x1 cell
    "#..#....#",
    "#..#...##",
    "#..#....#",
    "####....#",
    "...#....#",
    "...#....#",
    "...#...###",
]
CELL = 150
DEPTH = 300
LIFT = 200  # clears the BIOS's own "PlayStation" caption
FRONT = (0x40, 0xff, 0x80)  # phosphor green
SIDE = (0xd0, 0x90, 0x20)   # amber


def filled(x, y):
    return 0 <= y < len(GLYPH) and 0 <= x < len(GLYPH[y]) and GLYPH[y][x] == '#'


def logo_tmd():
    verts, vidx = [], {}
    normals = [(0, 0, -4096), (0, 0, 4096), (-4096, 0, 0), (4096, 0, 0), (0, -4096, 0), (0, 4096, 0)]
    prims = []
    w = max(len(r) for r in GLYPH)
    h = len(GLYPH)

    def v(x, y, z):
        key = (x, y, z)
        if key not in vidx:
            vidx[key] = len(verts)
            verts.append(((x - w / 2) * CELL, (y - h / 2) * CELL - LIFT, (z - 0.5) * DEPTH))
        return vidx[key]

    def quad(a, b, c, d, n, col):
        prims.append((col, n, a, b, c))
        prims.append((col, n, a, c, d))

    for y in range(h):
        for x in range(w):
            if not filled(x, y):
                continue
            quad(v(x, y, 0), v(x + 1, y, 0), v(x + 1, y + 1, 0), v(x, y + 1, 0), 0, FRONT)
            quad(v(x, y, 1), v(x, y + 1, 1), v(x + 1, y + 1, 1), v(x + 1, y, 1), 1, FRONT)
            if not filled(x - 1, y):
                quad(v(x, y, 0), v(x, y + 1, 0), v(x, y + 1, 1), v(x, y, 1), 2, SIDE)
            if not filled(x + 1, y):
                quad(v(x + 1, y, 0), v(x + 1, y, 1), v(x + 1, y + 1, 1), v(x + 1, y + 1, 0), 3, SIDE)
            if not filled(x, y - 1):
                quad(v(x, y, 0), v(x, y, 1), v(x + 1, y, 1), v(x + 1, y, 0), 4, SIDE)
            if not filled(x, y + 1):
                quad(v(x, y + 1, 0), v(x + 1, y + 1, 0), v(x + 1, y + 1, 1), v(x, y + 1, 1), 5, SIDE)

    # Offsets are relative to the object table (flags = 0).
    prim_top = 28
    prim_bytes = 16 * len(prims)
    vert_top = prim_top + prim_bytes
    norm_top = vert_top + 8 * len(verts)
    out = struct.pack('<III', 0x41, 0, 1)
    out += struct.pack('<IIIIIIi', vert_top, len(verts), norm_top, len(normals), prim_top, len(prims), 0)
    for col, n, a, b, c in prims:
        # F3, single-sided, light source calculation on
        out += struct.pack('<BBBB', 4, 3, 0x20, 0x20)
        out += struct.pack('<BBBB', *col, 0x20)
        out += struct.pack('<HHHH', n, a, b, c)
    for x, y, z in verts:
        out += struct.pack('<hhhh', int(x), int(y), int(z), 0)
    for x, y, z in normals:
        out += struct.pack('<hhhh', x, y, z, 0)
    return out


def text_sector():
    # The BIOS draws each 32-byte line as two 16-byte halves side by side,
    # so the title splits at its hyphen. Spacing set by eye in Redux with
    # a retail US BIOS.
    line1 = b'     FORTY-     ' + b'ONE CYCLES      '
    line2 = b'   a PlayStation' + b' demo by Rix    \n'
    s = line1 + line2
    fill = (b'0' * 62 + b'\n' + b'0')
    while len(s) < 2048:
        s += fill
    return s[:2048]


def main():
    tmd = logo_tmd()
    if len(tmd) > 7 * 2048:
        sys.exit('logo TMD is %d bytes, more than 7 sectors' % len(tmd))
    sectors = [(b'0' * 63 + b'\n') * 32] * 4 + [text_sector()]
    logo = tmd + b'\xff' * (7 * 2048 - len(tmd))
    sectors += [logo[i * 2048:(i + 1) * 2048] for i in range(7)]
    sub = bytes([0, 0, 8, 0, 0, 0, 8, 0])  # Form 1 data
    out = b''.join(sub + s + b'\0' * 280 for s in sectors)
    assert len(out) == 12 * 2336
    open(sys.argv[1], 'wb').write(out)
    print('%s: %d bytes, logo TMD %d bytes' % (sys.argv[1], len(out), len(tmd)))


main()
