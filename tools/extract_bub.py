"""Extract the dinosaur animation strips from the arcade sprite sheet.

The sheet has no alpha; cells sit on a flat light-blue grid. We key the
background with a flood fill from each frame's bounding box border, so interior
colours that happen to match the background are preserved.

Outputs build/preview/bub_frames.png (contact sheet, 4x, dark bg).
"""

import os
import sys
from collections import deque

from PIL import Image, ImageDraw, ImageFont

SRC = r"G:\work\基于rpgmaker素材的游戏制作\经典泡泡龙 图资\经典泡泡龙 sprites.png"
OUT = r"G:\work\基于rpgmaker素材的游戏制作\engine\hge-sdl\build\preview"

BG = {(147, 187, 236), (50, 97, 168), (96, 136, 176), (136, 160, 216)}

# band y0..y1, horizontal pitch, first frame x
BANDS = [
    ("green", 366, 404),
    ("blue", 415, 453),
]


def runs_for(im, y0, y1):
    px = im.load()
    W = im.size[0]
    cols = [any(px[x, y] not in BG for y in range(y0, y1 + 1)) for x in range(W)]
    runs, s = [], None
    for x, v in enumerate(cols + [False]):
        if v and s is None:
            s = x
        if not v and s is not None:
            runs.append((s, x - 1))
            s = None
    merged = []
    for a, b in runs:
        if merged and a - merged[-1][1] <= 3:
            merged[-1] = (merged[-1][0], b)
        else:
            merged.append((a, b))
    return merged


def key_box(im, box):
    """Flood-fill background from the border of box; returns RGBA crop."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0 + 1, y1 - y0 + 1
    src = im.crop((x0, y0, x1 + 1, y1 + 1)).convert("RGB")
    sp = src.load()
    seen = [[False] * w for _ in range(h)]
    dq = deque()

    def push(x, y):
        if 0 <= x < w and 0 <= y < h and not seen[y][x] and sp[x, y] in BG:
            seen[y][x] = True
            dq.append((x, y))

    for x in range(w):
        push(x, 0)
        push(x, h - 1)
    for y in range(h):
        push(0, y)
        push(w - 1, y)

    while dq:
        x, y = dq.popleft()
        push(x + 1, y)
        push(x - 1, y)
        push(x, y + 1)
        push(x, y - 1)

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    op = out.load()
    for y in range(h):
        for x in range(w):
            if not seen[y][x]:
                op[x, y] = (sp[x, y][0], sp[x, y][1], sp[x, y][2], 255)
    return out


def main():
    im = Image.open(SRC).convert("RGB")
    os.makedirs(OUT, exist_ok=True)
    all_frames = []

    for name, y0, y1 in BANDS:
        runs = runs_for(im, y0, y1)
        frames = []
        for (a, b) in runs:
            frames.append(key_box(im, (a, y0, b, y1)))
        all_frames.append((name, frames))
        print("%s: %d frames, sizes %s" %
              (name, len(frames), sorted({f.size for f in frames})))

    # contact sheet: one row per band, 4x, index labels
    Z = 4
    cells = max(len(f) for _, f in all_frames)
    cw, ch = 44 * Z, 44 * Z
    sheet = Image.new("RGB", (cells * (cw + 4) + 4, len(all_frames) * (ch + 22) + 4),
                      (26, 28, 34))
    d = ImageDraw.Draw(sheet)
    try:
        fnt = ImageFont.truetype("arial.ttf", 12)
    except Exception:
        fnt = ImageFont.load_default()

    for row, (name, frames) in enumerate(all_frames):
        y = 4 + row * (ch + 22)
        for i, fr in enumerate(frames):
            x = 4 + i * (cw + 4)
            big = fr.resize((fr.width * Z, fr.height * Z), Image.NEAREST)
            sheet.paste(big, (x, y + 16), big)
            d.text((x, y), "%s %d" % (name[0].upper(), i), font=fnt, fill=(200, 210, 230))
    sheet.save(os.path.join(OUT, "bub_frames.png"))
    print("wrote bub_frames.png", sheet.size)


if __name__ == "__main__":
    main()
