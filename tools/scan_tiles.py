"""Scan every 48x48 cell of the FSM tile sheets and rank them as *material
textures* for bubble fills: high opacity + high luminance variance.

Renders build/preview/scan.png so the result can be judged visually.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image, ImageDraw
import preview_assets as pa

SHEETS = [
    ("fsm_Forest01_A5", "A5"), ("fsm_Forest01_B", "B"),
    ("fsm_Forest01_C", "C"), ("fsm_Forest01_D", "D"),
    ("fsm_Forest01_E", "E"),
    ("fsm_Forest01n_B", "nB"), ("fsm_Forest01n_C", "nC"),
    ("fsm_Dungeon01_A5", "dA5"), ("fsm_Dungeon01_B", "dB"),
    ("fsm_Dungeon01_C", "dC"), ("fsm_Dungeon01_D", "dD"),
    ("fsm_Dungeon02_B", "eB"), ("fsm_Dungeon02_C", "eC"),
    ("fsm_extra02_C", "xC"), ("fsm_extra02_D", "xD"),
]

T = 48


def stats(cell):
    px = cell.load()
    w, h = cell.size
    lum = []
    r = g = b = n = 0
    opaque = 0
    for y in range(h):
        for x in range(w):
            R, G, B, A = px[x, y]
            if A > 200:
                opaque += 1
                l = (R * 299 + G * 587 + B * 114) // 1000
                lum.append(l)
                r += R
                g += G
                b += B
                n += 1
    if n == 0:
        return 0.0, 0, (0, 0, 0), 0
    mean = sum(lum) / len(lum)
    var = sum((l - mean) ** 2 for l in lum) / len(lum)
    return (var ** 0.5), opaque / (w * h), (r // n, g // n, b // n), n


def main():
    rows = []
    for sheet, tag in SHEETS:
        p = os.path.join(pa.TILES, sheet + ".png")
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("RGBA")
        names = pa.names_of(sheet)
        cols, nrows = im.size[0] // T, im.size[1] // T
        for r in range(nrows):
            for c in range(cols):
                cell = im.crop((c * T, r * T, c * T + T, r * T + T))
                sd, op, mean, n = stats(cell)
                if op < 0.97:
                    continue
                idx = r * 16 + c
                nm = names[idx] if idx < len(names) else "?"
                rows.append((sd, sheet, c, r, nm, op, mean))
    rows.sort(key=lambda t: -t[0])

    # diversity: at most 3 per sheet
    picked, per = [], {}
    for t in rows:
        if per.get(t[1], 0) >= 3:
            continue
        per[t[1]] = per.get(t[1], 0) + 1
        picked.append(t)
        if len(picked) >= 24:
            break

    OUT = os.path.join(r"G:\work\基于rpgmaker素材的游戏制作\engine\hge-sdl\build\preview")
    os.makedirs(OUT, exist_ok=True)
    f = pa.font(11)
    cellw = 112
    pad = 6
    per_row = 8
    nrows_img = (len(picked) + per_row - 1) // per_row
    W = per_row * (cellw + pad) + pad
    H = nrows_img * (cellw * 2 + 34) + pad
    img = Image.new("RGB", (W, H), (26, 26, 32))
    d = ImageDraw.Draw(img)

    tints = [(90, 170, 255), (110, 220, 120), (255, 90, 100),
             (190, 120, 255), (255, 190, 70), (120, 235, 225)]

    for i, (sd, sheet, c, r, nm, op, mean) in enumerate(picked):
        x = pad + (i % per_row) * (cellw + pad)
        y = pad + (i // per_row) * (cellw * 2 + 34)
        cell = pa.load_cell(sheet, c, r)
        big = cell.resize((cellw, cellw), Image.NEAREST)
        img.paste(big, (x, y + 26), big)
        bub = pa.make_bubble(cell, tints[i % 6], size=128, fill=mean)
        bub = bub.resize((cellw, cellw), Image.LANCZOS)
        img.paste(bub, (x, y + 26 + cellw), bub)
        d.text((x, y), "%s[%d,%d]" % (sheet.replace("fsm_", "").replace("_", ""), c, r),
               font=f, fill=(160, 200, 255))
        d.text((x, y + 11), nm[:18], font=f, fill=(235, 235, 235))
        d.text((x, y + 21), "sd%d" % int(sd), font=f, fill=(150, 150, 150))

    img.save(os.path.join(OUT, "scan.png"))
    print("wrote scan.png", img.size, "candidates", len(picked))
    for t in picked:
        print("  %-18s [%2d,%2d] sd=%5.1f  %-24s #%02X%02X%02X" %
              (t[1], t[2], t[3], t[0], t[4][:24], t[6][0], t[6][1], t[6][2]))


if __name__ == "__main__":
    main()
