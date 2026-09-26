#!/usr/bin/env python3
# make-icons.py - the application icon, from the Wilber in app/wilber.h.
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     python3 build-aux/make-icons.py
#
# writes data/icons/hicolor/<size>x<size>/apps/gimp42.png and
# build-aux/gimp42.ico.  The GIMP 1.0 Wilber is 76x59 on a flat grey; the
# grey that touches the edges becomes transparent and the picture is
# centred on a square.  Needs Pillow.

import os, re, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = (16, 24, 32, 48, 64, 128, 256)


def load_wilber():
    src = open(os.path.join(ROOT, 'app', 'wilber.h'), encoding='latin-1').read()
    w = int(re.search(r'wilber_width = (\d+)', src).group(1))
    h = int(re.search(r'wilber_height = (\d+)', src).group(1))
    body = src[src.index('wilber_data'):]
    body = body[body.index('=') + 1:]
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    data = ''.join(parts).encode('latin-1').decode('unicode_escape').encode('latin-1')
    img = Image.new('RGBA', (w, h))
    px = img.load()
    for i in range(w * h):
        d = data[i * 4:i * 4 + 4]
        r = (((d[0] - 33) << 2) | ((d[1] - 33) >> 4)) & 255
        g = ((((d[1] - 33) & 0xF) << 4) | ((d[2] - 33) >> 2)) & 255
        b = ((((d[2] - 33) & 0x3) << 6) | (d[3] - 33)) & 255
        px[i % w, i // w] = (r, g, b, 255)
    return img


def key_out_background(img):
    """Flood fill from the edges: the background grey becomes transparent."""
    w, h = img.size
    px = img.load()
    bg = px[0, 0][:3]

    def close(c):
        return sum(abs(a - b) for a, b in zip(c[:3], bg)) <= 24

    stack = [(x, y) for x in range(w) for y in (0, h - 1)]
    stack += [(x, y) for y in range(h) for x in (0, w - 1)]
    seen = set()
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        if not close(px[x, y]):
            continue
        px[x, y] = (0, 0, 0, 0)
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return img


def main():
    img = key_out_background(load_wilber())
    img = img.crop(img.getbbox())
    side = max(img.size) + 4
    square = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    square.paste(img, ((side - img.width) // 2, (side - img.height) // 2))

    for size in SIZES:
        out = os.path.join(ROOT, 'data', 'icons', 'hicolor', f'{size}x{size}', 'apps')
        os.makedirs(out, exist_ok=True)
        square.resize((size, size), Image.LANCZOS).save(os.path.join(out, 'gimp42.png'))

    square.resize((256, 256), Image.LANCZOS).save(
        os.path.join(ROOT, 'build-aux', 'gimp42.ico'),
        sizes=[(s, s) for s in SIZES])
    print('icons written')


if __name__ == '__main__':
    sys.exit(main())
