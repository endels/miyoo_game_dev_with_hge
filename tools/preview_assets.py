"""Build a contact sheet so the FSM tiles can be judged as bubble fills.

Outputs
  build/preview/tiles.png   raw candidates + sphere mockups
  build/preview/bg.png      parallax candidates
"""

import os
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

DLG = r"F:\SteamLibrary\steamapps\common\RPG Maker MV\dlc"
TILES = os.path.join(DLG, r"FSM_Woods_and_Cave_Tiles\Material\tilesets")
PARA = os.path.join(DLG, r"parallax_Resource_Pack\material\parallaxes")
OUT = r"G:\work\基于rpgmaker素材的游戏制作\engine\hge-sdl\build\preview"

T = 48


def load_cell(sheet, col, row):
    im = Image.open(os.path.join(TILES, sheet + ".png")).convert("RGBA")
    return im.crop((col * T, row * T, col * T + T, row * T + T))


def names_of(sheet):
    p = os.path.join(TILES, sheet + ".txt")
    if not os.path.exists(p):
        return []
    return [l.split("|")[0].strip() for l in open(p, encoding="utf-8").read().splitlines()]


def font(sz, bold=False):
    for f in (("arialbd.ttf" if bold else "arial.ttf"), "segoeui.ttf"):
        try:
            return ImageFont.truetype(f, sz)
        except Exception:
            pass
    return ImageFont.load_default()


def luminance(px):
    r, g, b, a = px
    return (r * 299 + g * 587 + b * 114) // 1000


def make_bubble(tile, tint, size=96, fill=None, shine=None):
    """Glass sphere whose inner material is the tile texture.

    Shading model: Lambert from the upper left, blue-ish rim light on the
    lower right, tight specular, inner refraction ring, 1px dark contour.
    """
    # smooth the material before upscaling: a crunchy 48px tile
    # magnified 4x is what made the sphere look blurry/noisy
    base = tile.filter(ImageFilter.GaussianBlur(0.6))
    src = base.resize((size, size), Image.BICUBIC)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    op = out.load()
    sp = src.load()

    R = size * 0.5 - 2.0
    cx = cy = size * 0.5 - 0.5
    tr, tg, tb = tint

    lx, ly, lz = -0.45, -0.55, 0.70
    n = math.sqrt(lx * lx + ly * ly + lz * lz)
    lx, ly, lz = lx / n, ly / n, lz / n

    hx, hy = cx - R * 0.33, cy - R * 0.35
    hr = R * 0.27

    for y in range(size):
        for x in range(size):
            dx = (x - cx) / R
            dy = (y - cy) / R
            d = math.sqrt(dx * dx + dy * dy)
            if d > 1.10:
                continue
            cov = min(1.0, max(0.0, (1.0 - d) * R + 0.5))
            if cov <= 0:
                continue

            nz = math.sqrt(max(0.0, 1.0 - min(d, 1.0) ** 2))
            diff = max(0.0, dx * lx + dy * ly + nz * lz)

            sr, sg, sb, sa = sp[min(x, src.size[0] - 1), min(y, src.size[1] - 1)]
            if fill is not None and sa < 200:
                sr, sg, sb = fill
            lum = luminance((sr, sg, sb, 255)) / 255.0

            # cartoon-ish: harder contrast, saturated, bright rim ring
            tex = 0.80 + 0.34 * lum
            dsh = 0.12 + 1.18 * diff                # strong sphere volume
            v = min(1.28, tex * dsh)
            if dy > 0.15:                            # contact shadow
                v *= 1.0 - 0.38 * ((dy - 0.15) / 0.85) * (0.55 + 0.45 * d)

            r = tr * v * 1.06
            g = tg * v * 1.06
            b = tb * v * 1.06

            # push colours apart so the fill reads as flat cartoon ink
            mid = (r + g + b) / 3.0
            r = mid + (r - mid) * 1.12
            g = mid + (g - mid) * 1.12
            b = mid + (b - mid) * 1.12

            # two-layer specular: broad sheen + tight hot spot
            sd = math.sqrt((x - hx) ** 2 + (y - hy) ** 2) / hr
            if sd < 1.7:
                sheen = max(0.0, 1.0 - sd / 1.7) ** 3.0
                r += 118 * sheen
                g += 123 * sheen
                b += 132 * sheen
            if sd < 0.50:
                hot = (1.0 - sd / 0.50) ** 1.2
                r += 215 * hot
                g += 220 * hot
                b += 215 * hot

            # bright ring that follows the outline: tint it with the bubble's
            # own colour, additive white turns grey over the dark edge
            ring = math.exp(-((d - 0.90) ** 2) / 0.0011)
            r += tr * 1.15 * ring
            g += tg * 1.15 * ring
            b += tb * 1.15 * ring

            # directional rim light on the lower right, tinted
            rimw = max(0.0, (d - 0.76) / 0.24)
            rim = max(0.0, dx * 0.55 + dy * 0.62 + nz * 0.52)
            r += tr * 0.95 * rimw * rim
            g += tg * 0.95 * rimw * rim
            b += tb * 0.95 * rimw * rim

            # animated specular sweep (0..1 along the diagonal)
            if shine is not None and d < 1.02:
                proj = dx * 0.7071 + dy * 0.7071
                p = -1.45 + 2.95 * shine
                band = math.exp(-((proj - p) ** 2) / 0.018) * 0.95
                r += 238 * band
                g += 242 * band
                b += 248 * band

            # heavier dark contour = cartoon ink line
            if d > 0.945:
                k = min(1.0, (d - 0.945) / 0.055)
                r *= 1.0 - 0.82 * k
                g *= 1.0 - 0.82 * k
                b *= 1.0 - 0.82 * k

            op[x, y] = (int(min(255, r)), int(min(255, g)), int(min(255, b)),
                        int(255 * cov))
    return out


CANDS = [
    ("Water",      "fsm_Forest01_A1", 0, 0),
    ("LeafTree",   "fsm_Forest01_C", 11, 9),
    ("Grass",      "fsm_Forest01_B", 1, 0),
    ("Flowers",    "fsm_Forest01_B", 4, 0),
    ("Mushrooms",  "fsm_Forest01_B", 15, 0),
    ("Boulder",    "fsm_Forest01_B", 5, 10),
    ("Barrel",     "fsm_Forest01_B", 4, 14),
    ("StoneBridge", "fsm_Forest01_D", 4, 0),
    ("Ore",        "fsm_Dungeon01_C", 10, 2),
    ("Firewood",   "fsm_Forest01_E", 10, 1),
]

TINTS = [
    (90, 170, 255),
    (110, 220, 120),
    (255, 90, 100),
    (190, 120, 255),
    (255, 190, 70),
    (120, 235, 225),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    zoom = 3
    cw, ch = T * zoom, T * zoom
    pad, lab = 12, 22
    cols = len(CANDS)
    W = cols * (cw + pad) + pad
    H = lab + (ch + lab) * 2 + 8
    img = Image.new("RGB", (W, H), (24, 24, 30))
    d = ImageDraw.Draw(img)
    f = font(13)

    for i, (label, sheet, c, r) in enumerate(CANDS):
        x = pad + i * (cw + pad)
        tile = load_cell(sheet, c, r)
        nm = names_of(sheet)
        idx = r * 16 + c
        full = nm[idx] if idx < len(nm) else "?"
        d.text((x, 2), "%d:%s" % (i, label), font=f, fill=(230, 230, 240))
        d.text((x, 14), full[:22], font=f, fill=(150, 200, 255))
        big = tile.resize((cw, ch), Image.NEAREST)
        img.paste(big, (x, lab), big)

        b = make_bubble(tile, TINTS[i % len(TINTS)], size=128)
        b = b.resize((cw, ch), Image.LANCZOS)
        img.paste(b, (x, lab + ch + lab), b)

    img.save(os.path.join(OUT, "tiles.png"))
    print("wrote tiles.png", img.size, "candidates:", len(CANDS))

    bgs = ["AncientForest.png", "ForestNight.png", "ForestSpiritual.png",
           "ShadowForest.png", "ForestAutumn.png", "ForestSummer.png"]
    bw = 360
    bh = int(bw * 540 / 960)
    sheet_img = Image.new("RGB", (3 * (bw + 10) + 10, 2 * (bh + 20) + 10), (24, 24, 30))
    dd = ImageDraw.Draw(sheet_img)
    for i, name in enumerate(bgs):
        p = os.path.join(PARA, name)
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("RGB")
        s = max(bw / im.width, bh / im.height)
        im = im.resize((int(im.width * s), int(im.height * s)), Image.LANCZOS)
        left = (im.width - bw) // 2
        top = (im.height - bh) // 2
        im = im.crop((left, top, left + bw, top + bh))
        x = 10 + (i % 3) * (bw + 10)
        y = 10 + (i // 3) * (bh + 20)
        sheet_img.paste(im, (x, y))
        dd.text((x, y + bh + 2), name, font=f, fill=(220, 220, 230))
    sheet_img.save(os.path.join(OUT, "bg.png"))
    print("wrote bg.png", sheet_img.size)


if __name__ == "__main__":
    main()
