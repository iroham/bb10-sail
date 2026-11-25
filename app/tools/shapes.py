#!/usr/bin/env python3
"""Sail: draw the rounded shapes Cascades can't draw itself (containers have square corners), as nine-slice
images: assets/shapes/<name>.png + <name>.amd. Usage: python3 tools/shapes.py"""
import os, struct, zlib
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "shapes")
def rgb(h): h = h.lstrip("#"); return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
def png(path, w, h, px):
    raw = b"".join(b"\0" + bytes(c for x in range(w) for c in px(x, y)) for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
def cover(x, y, size, r, inset=0.0, n=4):
    """Share of pixel (x, y) inside a rounded square of this size and corner radius, shrunk by inset."""
    hit = 0
    for i in range(n):
        for j in range(n):
            px, py = x + (i + .5) / n, y + (j + .5) / n
            lo, hi = inset + r, size - inset - r
            cx, cy = min(max(px, lo), hi), min(max(py, lo), hi)
            hit += (px - cx) ** 2 + (py - cy) ** 2 <= r * r
    return hit / (n * n)
def shape(name, size, radius, fill, ring=None, ring_w=0):
    f, rg = rgb(fill), rgb(ring) if ring else None
    def px(x, y):
        a = cover(x, y, size, radius)
        if not rg: return (*f, round(255 * a))
        inner = cover(x, y, size, max(radius - ring_w, 0), ring_w)      # fill inside the ring
        col = tuple(round(rg[k] + (f[k] - rg[k]) * (inner / a if a else 0)) for k in range(3))
        return (*col, round(255 * a))
    png(os.path.join(OUT, name + ".png"), size, size, px)
    s = size // 2 - 1
    open(os.path.join(OUT, name + ".amd"), "w").write(           # format as in the system apps' own assets
        '#RimCascadesAssetMetaData version=1.0\nsliceMargins:%d %d %d %d\nsource: "%s.png"\n' % (s, s, s, s, name))
os.makedirs(OUT, exist_ok=True)
shape("field", 100, 50, "#23262B")                                   # the address pill
shape("field_edit", 100, 50, "#23262B", ring="#62A8FF", ring_w=3)    # the same, focused
shape("press", 100, 50, "#2E3238")                                   # pressed highlight behind an icon button
shape("panel", 72, 28, "#1D2024", ring="#2E3238", ring_w=2)          # the menu
shape("button", 96, 48, "#62A8FF")                                   # the one filled button
shape("button_down", 96, 48, "#4B8FE6")
print("shapes ->", os.path.normpath(OUT))
