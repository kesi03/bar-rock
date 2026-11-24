#!/usr/bin/env python3
"""Draw the BAR-ROCK icon as a 960x540 PNG using only the Python stdlib.

A Baroque portrait motif: a powdered 1700s full-bottom wig with curls, ribbon
and queue, framed by gold scrollwork above a harpsichord keyboard.

Run from the repository root:  python tools/make_icon.py
Writes firmware/bar-rock/icon.png
"""

import math
import struct
import zlib
from pathlib import Path

W, H = 960, 540
SS = 2
SW, SH = W * SS, H * SS

NIGHT = (16, 12, 20)
PANEL = (29, 20, 40)
PANEL2 = (42, 27, 57)
ORANGE = (255, 159, 28)
GOLD = (242, 193, 78)
CREAM = (255, 240, 208)
AVO = (143, 164, 75)
VIOLET = (185, 135, 255)
BLUE = (70, 182, 255)
DARK = (9, 6, 13)
INK = (38, 24, 44)
VELVET = (94, 32, 64)
POWDER = (234, 232, 222)
POWDER_HI = (255, 251, 238)
POWDER_SHADE = (164, 168, 170)
SKIN = (242, 212, 180)
SKIN_SHADE = (198, 160, 132)


def blank():
    px = bytearray(SW * SH * 3)
    for y in range(SH):
        t = y / SH
        base = (NIGHT[0] + int(18 * t), NIGHT[1] + int(8 * t), NIGHT[2] + int(26 * t))
        o = y * SW * 3
        px[o:o + SW * 3] = bytes(base) * SW
    return px


def put(px, x, y, color):
    if 0 <= x < SW and 0 <= y < SH:
        o = (y * SW + x) * 3
        px[o:o + 3] = bytes(color)


def rect(px, x0, y0, x1, y1, color):
    x0, y0 = int(x0 * SS), int(y0 * SS)
    x1, y1 = int(x1 * SS), int(y1 * SS)
    x0, x1 = max(0, x0), min(SW, x1)
    y0, y1 = max(0, y0), min(SH, y1)
    row = bytes(color) * (x1 - x0)
    for y in range(y0, y1):
        o = y * SW * 3 + x0 * 3
        px[o:o + len(row)] = row


def ellipse(px, cx, cy, rx, ry, color, inner=None):
    cx, cy, rx, ry = cx * SS, cy * SS, rx * SS, ry * SS
    irx = iry = 0
    if inner is not None:
        irx, iry, inner = inner[0] * SS, inner[1] * SS, inner[2]
    reach = max(rx, ry) + 2
    for y in range(max(0, int(cy - reach)), min(SH, int(cy + reach + 1))):
        dy = y - cy
        for x in range(max(0, int(cx - reach)), min(SW, int(cx + reach + 1))):
            dx = x - cx
            if (dx / rx) ** 2 + (dy / ry) ** 2 <= 1.0:
                col = color
                if inner is not None and irx > 0 and iry > 0:
                    if (dx / irx) ** 2 + (dy / iry) ** 2 < 1.0:
                        col = inner
                put(px, x, y, col)


def circle(px, cx, cy, r, color, inner=None):
    ellipse(px, cx, cy, r, r, color, inner=inner)


def ring(px, cx, cy, r, thick, color):
    cx, cy, r, thick = cx * SS, cy * SS, r * SS, thick * SS
    lo, hi = (r - thick) ** 2, (r + thick) ** 2
    for y in range(max(0, int(cy - r - thick)), min(SH, int(cy + r + thick + 1))):
        dy = y - cy
        for x in range(max(0, int(cx - r - thick)), min(SW, int(cx + r + thick + 1))):
            d = (x - cx) ** 2 + dy * dy
            if lo <= d <= hi:
                put(px, x, y, color)


def line(px, x0, y0, x1, y1, width, color):
    x0, y0, x1, y1 = x0 * SS, y0 * SS, x1 * SS, y1 * SS
    w = width * SS
    dx, dy = x1 - x0, y1 - y0
    ln2 = dx * dx + dy * dy
    for y in range(max(0, int(min(y0, y1) - w - 2)), min(SH, int(max(y0, y1) + w + 3))):
        for x in range(max(0, int(min(x0, x1) - w - 2)), min(SW, int(max(x0, x1) + w + 3))):
            t = 0.0 if ln2 == 0 else max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / ln2))
            qx, qy = x0 + t * dx, y0 + t * dy
            if (x - qx) ** 2 + (y - qy) ** 2 <= w * w:
                put(px, x, y, color)


def arc(px, cx, cy, r, a0, a1, thick, color):
    cx, cy, r, w = cx * SS, cy * SS, r * SS, thick * SS
    steps = max(16, int(abs(a1 - a0) * r / 5))
    for i in range(steps + 1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        for yy in range(int(y - w - 1), int(y + w + 2)):
            for xx in range(int(x - w - 1), int(x + w + 2)):
                if (xx - x) ** 2 + (yy - y) ** 2 <= w * w:
                    put(px, xx, yy, color)


def round_rect(px, x0, y0, x1, y1, r, color):
    cx0, cy0 = (x0 + r) * SS, (y0 + r) * SS
    cx1, cy1 = (x1 - r) * SS, (y1 - r) * SS
    r2 = (r * SS) ** 2
    for y in range(max(0, int(y0 * SS)), min(SH, int(y1 * SS) + 1)):
        for x in range(max(0, int(x0 * SS)), min(SW, int(x1 * SS) + 1)):
            dx = dy = 0.0
            if x < cx0:
                dx = x - cx0
            elif x > cx1:
                dx = x - cx1
            if y < cy0:
                dy = y - cy0
            elif y > cy1:
                dy = y - cy1
            if dx * dx + dy * dy <= r2:
                put(px, x, y, color)


def curl(px, cx, cy, rx, ry, highlight=POWDER_HI):
    """A powdered sausage curl with a shaded edge and a small inner gleam."""
    ellipse(px, cx, cy, rx, ry, POWDER_SHADE, inner=(rx * 0.80, ry * 0.78, POWDER))
    ellipse(px, cx - rx * 0.22, cy - ry * 0.18,
            rx * 0.44, ry * 0.38, highlight)


def downsample(px):
    out = bytearray(W * H * 3)
    n = SS * SS
    for y in range(H):
        for x in range(W):
            r = g = b = 0
            for yy in range(SS):
                row = (y * SS + yy) * SW * 3
                for xx in range(SS):
                    o = row + (x * SS + xx) * 3
                    r += px[o]
                    g += px[o + 1]
                    b += px[o + 2]
            o = (y * W + x) * 3
            out[o] = r // n
            out[o + 1] = g // n
            out[o + 2] = b // n
    return out


def png(rows):
    raw = b''.join(b'\x00' + bytes(row) for row in rows)

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def frame(px):
    rect(px, 0, 0, W, 18, ORANGE)
    rect(px, 0, H - 22, W, H, AVO)
    rect(px, 0, 66, W, 70, PANEL2)
    rect(px, 0, H - 74, W, H - 69, PANEL2)
    # Gold C-scrolls in the upper corners, the Baroque calling card.
    for cx, flip in ((86, 1), (W - 86, -1)):
        arc(px, cx, 104, 46, 210 if flip > 0 else 330, 510 if flip > 0 else 630, 4, GOLD)
        arc(px, cx + flip * 8, 104, 27, 20 if flip > 0 else 160, 200 if flip > 0 else 340, 3, ORANGE)
        circle(px, cx, 104, 7, GOLD)
    for cx in (150, W - 150):
        arc(px, cx, 72, 20, 180, 360, 2, (104, 70, 124))


def speed_lines(px):
    for y, x0, x1, col in (
        (116, 30, 190, VIOLET), (158, 10, 150, BLUE), (214, 36, 186, ORANGE),
        (292, 24, 174, BLUE), (338, 62, 206, VIOLET)
    ):
        rect(px, x0, y, x1, y + 4, col)
    for y, x0, x1 in ((132, 770, 928), (188, 796, 944), (252, 760, 916),
                      (306, 788, 938), (350, 810, 950)):
        rect(px, x0, y, x1, y + 3, (72, 48, 98))


def wig(px):
    # The gold 8' disc sits behind the portrait.
    ring(px, 480, 208, 170, 7, GOLD)
    circle(px, 480, 208, 157, GOLD, inner=(140, 140, (207, 151, 48)))

    # Back mass and the low shoulder curls of a full-bottom wig.
    ellipse(px, 480, 226, 170, 140, POWDER_SHADE, inner=(150, 120, POWDER))
    for y in (158, 200, 242, 282):
        curl(px, 334, y, 42, 24, POWDER_HI)
        curl(px, 626, y, 42, 24, POWDER_HI)

    # Queue and velvet bow at the back right.
    line(px, 606, 208, 650, 254, 10, POWDER_SHADE)
    curl(px, 660, 272, 32, 20, POWDER_HI)
    ellipse(px, 606, 204, 24, 13, VELVET, inner=(15, 7, ORANGE))
    ellipse(px, 630, 204, 24, 13, VELVET, inner=(15, 7, ORANGE))
    circle(px, 618, 204, 11, GOLD)

    # Crown and centre parting.
    ellipse(px, 480, 126, 130, 84, POWDER_SHADE, inner=(114, 70, POWDER_HI))
    for x in (420, 480, 540):
        curl(px, x, 78, 40, 26, POWDER_HI)
    line(px, 480, 56, 480, 112, 4, POWDER_SHADE)
    line(px, 480, 112, 458, 146, 3, POWDER_SHADE)
    line(px, 480, 112, 502, 146, 3, POWDER_SHADE)

    # Face, just enough to read as a portrait.
    ellipse(px, 480, 190, 58, 74, SKIN)
    ellipse(px, 462, 190, 34, 58, (230, 195, 162))
    line(px, 452, 176, 468, 174, 3, INK)
    line(px, 492, 174, 508, 176, 3, INK)
    circle(px, 460, 186, 4, INK)
    circle(px, 500, 186, 4, INK)
    line(px, 480, 186, 477, 206, 3, SKIN_SHADE)
    line(px, 468, 218, 492, 218, 3, VELVET)

    # Side rolls frame the face; overlap is deliberate, it is how a wig sits.
    for x, y, rx, ry in (
        (408, 132, 31, 44), (400, 176, 33, 44),
        (404, 220, 33, 40), (416, 258, 34, 26),
        (552, 132, 31, 44), (560, 176, 33, 44),
        (556, 220, 33, 40), (544, 258, 34, 26)
    ):
        curl(px, x, y, rx, ry, POWDER_HI)


def keyboard(px):
    # Coat and cravat under the portrait.
    round_rect(px, 300, 298, 660, 440, 30, INK)
    round_rect(px, 316, 310, 644, 424, 26, VIOLET)
    line(px, 480, 310, 480, 412, 4, GOLD)
    line(px, 452, 330, 508, 330, 3, CREAM)
    line(px, 460, 344, 500, 344, 3, CREAM)

    # A few strings above the keys keep the instrument readable.
    for i in range(12):
        x = 252 + i * 40
        line(px, x, 366, x + 34, 382, 2, GOLD)

    round_rect(px, 206, 386, 754, 464, 18, DARK)
    round_rect(px, 214, 394, 746, 456, 14, VIOLET)
    rect(px, 214, 394, 746, 400, GOLD)

    x0, x1, count = 222.0, 738.0, 14
    kw = (x1 - x0) / count
    for i in range(count):
        x = x0 + i * kw
        rect(px, x, 406, x + kw - 2, 450, CREAM)
        rect(px, x + 3, 444, x + kw - 5, 449, (196, 174, 143))
    for i in (0, 1, 3, 4, 5, 7, 8, 10, 11):
        bx = x0 + (i + 1) * kw - kw * 0.30
        rect(px, bx, 406, bx + kw * 0.60, 434, DARK)


def main():
    px = blank()
    frame(px)
    speed_lines(px)
    wig(px)
    keyboard(px)

    small = downsample(px)
    rows = [small[y * W * 3:(y + 1) * W * 3] for y in range(H)]
    out = Path(__file__).resolve().parent.parent / 'firmware' / 'bar-rock' / 'icon.png'
    out.write_bytes(png(rows))
    print(f'wrote {out} ({out.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
