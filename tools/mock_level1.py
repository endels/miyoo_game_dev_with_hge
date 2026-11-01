"""Mock screenshot of Bubble Level 1 at the real 960x540 layout, so the look
can be approved before any game code is written.
"""

import os
import sys
import math

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image, ImageDraw, ImageFont, ImageFilter
import preview_assets as pa

OUT = r"G:\work\基于rpgmaker素材的游戏制作\engine\hge-sdl\build\preview"
PARA = r"F:\SteamLibrary\steamapps\common\RPG Maker MV\dlc\parallax_Resource_Pack\material\parallaxes"

W, H = 960, 540
FIELD_W = 640
D = 40
R = D // 2
ROW_H = D * math.sqrt(3) / 2          # 34.64

# (sheet, col, row) chosen for texture quality, plus final colour
BUBBLES = [
    ("fsm_Forest01_A5",   4,  5, (248, 205, 70)),    # rock/moss  -> yellow
    ("fsm_Forest01_B",   13, 10, (92, 152, 246)),    # water edge -> blue
    ("fsm_Forest01_B",   11, 11, (110, 214, 96)),    # swamp reed -> green
    ("fsm_Forest01_B",    6, 10, (238, 84, 96)),     # wall grass -> red
    ("fsm_Dungeon02_C",  15, 11, (176, 122, 250)),   # stone      -> purple
    ("fsm_Dungeon02_C",  15,  0, (86, 226, 222)),    # coral      -> cyan
]


def font(sz, bold=False):
    names = ["msyhbd.ttc" if bold else "msyh.ttc", "arialbd.ttf" if bold else "arial.ttf"]
    for n in names:
        p = os.path.join(r"C:\Windows\Fonts", n)
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, sz)
            except Exception:
                pass
    return ImageFont.load_default()


def normalize(cell, target=132.0, gain=1.55):
    """Even out luminance so every bubble is equally readable after tinting."""
    out = Image.new("RGBA", cell.size, (0, 0, 0, 0))
    sp = cell.load()
    op = out.load()
    lums = []
    for y in range(cell.size[1]):
        for x in range(cell.size[0]):
            r, g, b, a = sp[x, y]
            if a > 200:
                lums.append((r * 299 + g * 587 + b * 114) / 1000.0)
    if not lums:
        return cell
    mean = sum(lums) / len(lums)
    for y in range(cell.size[1]):
        for x in range(cell.size[0]):
            r, g, b, a = sp[x, y]
            if a <= 200:
                op[x, y] = (0, 0, 0, 0)
                continue
            l = (r * 299 + g * 587 + b * 114) / 1000.0
            ln = max(8.0, min(255.0, target + (l - mean) * gain))
            k = ln / max(l, 1.0)
            op[x, y] = (int(min(255, r * k)), int(min(255, g * k)), int(min(255, b * k)), a)
    return out


def bubble_at_size(cell, color, size, shine=None):
    n = normalize(cell)
    px = n.load()
    # mean of normalised tile, used where the tile is transparent
    rs = gs = bs = cnt = 0
    for y in range(n.size[1]):
        for x in range(n.size[0]):
            r, g, b, a = px[x, y]
            if a > 200:
                rs += r; gs += g; bs += b; cnt += 1
    fill = (rs // max(cnt, 1), gs // max(cnt, 1), bs // max(cnt, 1))
    im = pa.make_bubble(n, color, size=size * 4, fill=fill, shine=shine)
    return im.resize((size, size), Image.LANCZOS)


def cover(im, w, h):
    s = max(w / im.width, h / im.height)
    im = im.resize((int(im.width * s + 0.5), int(im.height * s + 0.5)), Image.LANCZOS)
    l = (im.width - w) // 2
    t = (im.height - h) // 2
    return im.crop((l, t, l + w, t + h))


def main():
    os.makedirs(OUT, exist_ok=True)

    bg = Image.open(os.path.join(PARA, "ForestNight.png")).convert("RGB")
    bg = cover(bg, W, H)
    bg = bg.filter(ImageFilter.GaussianBlur(2.2))
    # darken + cool tone so bright bubbles pop
    bg = Image.eval(bg, lambda v: int(v * 0.66))
    bg = Image.blend(bg, Image.new("RGB", (W, H), (10, 14, 26)), 0.28)

    img = bg.copy()
    d = ImageDraw.Draw(img, "RGBA")

    # playfield panel
    d.rectangle([0, 0, FIELD_W - 1, H - 1], fill=(6, 9, 16, 90))
    d.rectangle([FIELD_W - 1, 0, FIELD_W, H], fill=(70, 90, 120, 255))
    # ceiling bar
    d.rectangle([0, 0, FIELD_W - 1, 7], fill=(44, 56, 74, 255))
    d.rectangle([0, 7, FIELD_W - 1, 9], fill=(90, 112, 140, 255))

    # ---- grid ---------------------------------------------------------
    cells = {}           # (row, col) -> bubble index

    def x_of(row, col):
        parity = row & 1
        return (20 + col * D) if parity == 0 else (40 + col * D)

    def y_of(row):
        return 8 + R + row * ROW_H

    layout = [
        "1 2 3 0 1 2 3 0 1 2 3 0 1 2 3 0",
        "2 3 0 1 2 3 0 1 2 3 0 1 2 3 0",
        "3 0 1 2 3 0 1 2 3 0 1 2 3 0 1 2",
        "0 1 2 3 . . 2 3 0 1 . . 1 2 3",
        "1 2 . . 3 0 1 2 3 0 1 . . 2 1",
    ]
    colors = [1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0]
    for r, line in enumerate(layout):
        vals = line.split()
        for c, tok in enumerate(vals):
            if tok == ".":
                continue
            cells[(r, c)] = int(tok)

    # bubbles are pre-rendered once per colour at 4x then downscaled
    sprites = [bubble_at_size(pa.load_cell(s, c, r), col, D)
               for (s, c, r, col) in BUBBLES]

    # draw back-to-front so the hex overlap looks right
    for (r, c), ci in sorted(cells.items(), key=lambda kv: kv[0][0]):
        x = x_of(r, c) - R
        y = y_of(r) - R
        img.paste(sprites[ci], (int(x), int(y)), sprites[ci])

    # death line
    dl = 470
    for x in range(0, FIELD_W, 28):
        d.line([x, dl, x + 14, dl], fill=(232, 90, 90, 190), width=2)
    d.text((10, dl + 4), "DEATH LINE", font=font(11), fill=(232, 120, 120, 220))

    # shooter
    sx, sy = FIELD_W // 2, 498
    d.ellipse([sx - 26, sy - 26, sx + 26, sy + 26], fill=(30, 38, 52, 255),
              outline=(96, 120, 150, 255), width=2)
    ang = -math.pi / 2 - 0.22
    ex, ey = sx + math.cos(ang) * 34, sy + math.sin(ang) * 34
    d.line([sx, sy, ex, ey], fill=(150, 175, 205, 255), width=9)
    img.paste(sprites[2], (sx - R, sy - R), sprites[2])

    # aim dots
    for i in range(1, 11):
        px = sx + math.cos(ang) * (40 + i * 22)
        py = sy + math.sin(ang) * (40 + i * 22)
        if py < 20:
            break
        rr = 3 if i % 2 else 2
        d.ellipse([px - rr, py - rr, px + rr, py + rr], fill=(255, 255, 255, 130))

    # ---- HUD ----------------------------------------------------------
    hx = FIELD_W + 18
    d.text((hx, 24), "LEVEL 1", font=font(26, True), fill=(235, 240, 250, 255))
    d.text((hx, 56), "Whispering Woods", font=font(13), fill=(150, 170, 195, 255))
    d.line([hx, 80, W - 18, 80], fill=(80, 100, 125, 255), width=1)

    d.text((hx, 96), "NEXT", font=font(12), fill=(150, 170, 195, 255))
    d.rounded_rectangle([hx, 112, hx + 56, 168], 8, fill=(12, 17, 27, 220),
                        outline=(80, 100, 125, 255))
    img.paste(sprites[3], (hx + 8, 120), sprites[3])

    d.text((hx, 196), "SHOTS", font=font(12), fill=(150, 170, 195, 255))
    d.text((hx, 212), "12", font=font(30, True), fill=(235, 240, 250, 255))

    d.text((hx, 264), "SCORE", font=font(12), fill=(150, 170, 195, 255))
    d.text((hx, 280), "0", font=font(30, True), fill=(235, 240, 250, 255))

    d.text((hx, 340), "NEXT ROW IN", font=font(12), fill=(150, 170, 195, 255))
    d.text((hx, 356), "6 shots", font=font(16, True), fill=(235, 240, 250, 255))

    d.text((hx, 470), "AIM: mouse", font=font(11), fill=(120, 140, 165, 255))
    d.text((hx, 486), "FIRE: click / space", font=font(11), fill=(120, 140, 165, 255))
    d.text((hx, 502), "ESC: quit", font=font(11), fill=(120, 140, 165, 255))

    # swatch strip of all 6 colours at real size
    d.text((hx, 406), "COLOURS", font=font(12), fill=(150, 170, 195, 255))
    for i, s in enumerate(sprites):
        img.paste(s, (hx + i * 30, 424), s)

    img.save(os.path.join(OUT, "mock_level1.png"))
    print("wrote mock_level1.png", img.size)


if __name__ == "__main__":
    main()
