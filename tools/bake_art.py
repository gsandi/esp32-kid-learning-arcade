#!/usr/bin/env python3
"""Bake the art partition: dial bitmaps + launcher icons -> build/art.bin.

The P4 app slot is 4 MB and the two Orb dials alone are 870 KB, so bitmaps live in
a dedicated `art` data partition (see partitions.csv) that the firmware memory-maps
at boot (src/art_store.cpp). This script is the producer.

Layout (all little-endian):
    header   'KART' u32 magic | u32 version=1 | u32 count | u32 reserved
    entries  count x { char name[16] (NUL padded) | u32 offset | u16 w | u16 h |
                       u8 format (0 RGB565, 1 RGB565A8 planar) | u8 pad[3] | u32 size }
    blobs    each 16-byte aligned, at `offset` from the start of the file

RGB565A8 is LVGL 9's planar layout: the whole RGB565 map, then the whole A8 map.

Sources:
    assets/orb/dial_avi.h, assets/orb/dial_img.h   (uint16 arrays from orb-os)
    assets/icons/*.png                              (launcher icons, any size, RGBA)

Usage:  python3 tools/bake_art.py [out.bin]
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'art.bin')

def parse_u16_header(path, w, h):
    txt = open(path).read()
    body = txt[txt.index('{') + 1: txt.rindex('}')]
    vals = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]{4}', body)]
    assert len(vals) == w * h, f'{path}: {len(vals)} values, expected {w*h}'
    return struct.pack('<%dH' % len(vals), *vals)

def png_rgb565a8(path):
    from PIL import Image
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    px = im.load()
    rgb = bytearray(); a = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, al = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            rgb += struct.pack('<H', v); a.append(al)
    return w, h, bytes(rgb) + bytes(a)

entries = []  # (name, w, h, fmt, blob)
entries.append(('dial_avi', 466, 466, 0, parse_u16_header(os.path.join(ROOT, 'assets/orb/dial_avi.h'), 466, 466)))
entries.append(('dial_img', 466, 466, 0, parse_u16_header(os.path.join(ROOT, 'assets/orb/dial_img.h'), 466, 466)))

icon_dir = os.path.join(ROOT, 'assets/icons')
if os.path.isdir(icon_dir):
    for f in sorted(os.listdir(icon_dir)):
        if f.lower().endswith('.png'):
            name = 'icon_' + os.path.splitext(f)[0]
            w, h, blob = png_rgb565a8(os.path.join(icon_dir, f))
            entries.append((name[:15], w, h, 1, blob))

hdr_size = 16 + 32 * len(entries)
off = (hdr_size + 15) & ~15
table = b''; blobs = b''
for name, w, h, fmt, blob in entries:
    assert len(name) <= 15
    table += struct.pack('<16sIHHB3xI', name.encode(), off, w, h, fmt, len(blob))
    pad = (-len(blob)) & 15
    blobs += blob + b'\0' * pad
    print(f'  {name:16s} {w}x{h} fmt={fmt} {len(blob):8d} B @ 0x{off:06X}')
    off += len(blob) + pad
out = struct.pack('<4sIII', b'KART', 1, len(entries), 0) + table
out += b'\0' * (((hdr_size + 15) & ~15) - hdr_size) + blobs
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, 'wb').write(out)
print(f'wrote {OUT}: {len(out)} bytes, {len(entries)} entries')
