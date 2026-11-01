"""Compare downscale strategies for the arcade bubbles (96px source -> 40px).

Writes build/preview/crisp_cmp.png: each variant at 6x for inspection.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_bub as E
import bake_game_assets as B

OUT = r"G:\work\基于rpgmaker素材的游戏制作\engine\hge-sdl\build\preview"


def to40_lanczos(src):
    return src.resize((40, 40), Image.LANCZOS)


def hard_alpha(im, lo=70, hi=150):
    r, g, b, a = im.split()
    a = a.point(lambda v: 0 if v < lo else (255 if v > hi else v))
    return Image.merge("RGBA", (r, g, b, a))


def to40_crisp(src):
    sm = src.resize((40, 40), Image.LANCZOS)
    sm = hard_alpha(sm)
    rgb = sm.convert("RGB").filter(ImageFilter.UnsharpMask(radius=0.9, percent=170, threshold=1))
    a = sm.split()[3]
    rgb.putalpha(a)
    # restore flat pixel-art regions
    rgb = rgb.convert("RGB").point(lambda v: (v // 17) * 17)
    rgb.putalpha(a)
    return rgb


def to40_rim(src):
    im = to40_crisp(src)
    px = im.load()
    w, h = im.size
    a0 = [px[x, y][3] > 128 for y in range(h) for x in range(w)]
    out = im.copy()
    op = out.load()
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            if px[x, y][3] > 128:
                continue
            near = any(px[x + dx, y + dy][3] > 128
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if near:
                op[x, y] = (24, 20, 32, 235)
    return out


def main():
    src = B._arcade_bubble(1)          # blue
    variants = [("lanczos", to40_lanczos(src)),
                ("hard A", to40_crisp(src)),
                ("hard A + rim", to40_rim(src))]
    Z = 8
    cw = 40 * Z
    sheet = Image.new("RGB", (len(variants) * (cw + 14) + 14, cw + 40), (26, 28, 34))
    d = ImageDraw.Draw(sheet)
    try:
        f = ImageFont.truetype("arial.ttf", 14)
    except Exception:
        f = ImageFont.load_default()
    for i, (name, im) in enumerate(variants):
        x = 14 + i * (cw + 14)
        big = im.resize((cw, cw), Image.NEAREST)
        sheet.paste(big, (x, 28), big)
        d.text((x, 6), name, font=f, fill=(220, 230, 240))
    sheet.save(os.path.join(OUT, "crisp_cmp.png"))
    print("wrote crisp_cmp.png", sheet.size)


if __name__ == "__main__":
    main()
