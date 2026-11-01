"""Bake the Bubble Level 1 asset pack from FSM / parallax sources.

Output: engine/hge-sdl/assets/game.pak

Format
    char  magic[4]      "HGEP"
    u32   version       1
    u32   count
    count x { char name[24]; u32 w; u32 h; u32 bytes }
    blobs, in the same order

Pixels are **B,G,R,A** (i.e. HGE's 0xAARRGGBB in little endian), so the C++
side can hand them straight to IGfx::CreateTexture without conversion.
"""

import os
import sys
import math
import struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageFilter
import preview_assets as pa
from mock_level1 import BUBBLES, normalize, bubble_at_size, cover

ENGINE = r"G:\work\基于rpgmaker素材的游戏制作\engine\hge-sdl"
OUT_PAK = os.path.join(ENGINE, "assets", "game.pak")
PARA = r"F:\SteamLibrary\steamapps\common\RPG Maker MV\dlc\parallax_Resource_Pack\material\parallaxes"

WIN_W, WIN_H = 960, 540

FONT_CW, FONT_CH, FONT_COLS = 20, 26, 16


def to_bgra(im):
    r, g, b, a = im.convert("RGBA").split()
    return Image.merge("RGBA", (b, g, r, a))


ARCADE = r"G:\work\基于rpgmaker素材的游戏制作\经典泡泡龙 图资\经典泡泡龙 sprites.png"

# Large bubble band (y0, y1) per colour index 1..6:
# yellow, blue, green, red, purple, pink
AR_BANDS = [
    (786, 881),
    (592, 687),
    (980, 1075),
    (689, 784),
    (883, 978),
    (1086, 1249),
]

_ARC_BG = {(147, 187, 236), (50, 97, 168), (96, 136, 176), (136, 160, 216)}


def _hue_shift(im, deg):
    """Rotate hue, keep alpha (used to derive a 6th colour)."""
    hsv = im.convert("RGB").convert("HSV")
    h, s, v = hsv.split()
    off = int(round(deg * 255.0 / 360.0))
    h = h.point([(x + off) % 256 for x in range(256)])
    out = Image.merge("HSV", (h, s, v)).convert("RGB")
    out.putalpha(im.split()[3])
    return out


def _arcade_bubble(idx):
    """Fixed 96x96 window on the big normal bubble of colour `idx`.

    The sheet only ships five distinct bubble colours; the sixth is the first
    one with the hue rotated.
    """
    import extract_bub as E

    if idx >= 5:
        return _hue_shift(_arcade_bubble(0), -28.0)

    src = Image.open(ARCADE).convert("RGB")
    px = src.load()
    y0, y1 = AR_BANDS[idx]
    ys = [y for y in range(y0, y1 + 1)
          for x in range(99, 195) if px[x, y] not in _ARC_BG]
    if not ys:
        raise RuntimeError("no bubble in band %s" % (AR_BANDS[idx],))
    a = min(ys)
    return E.key_box(src, (99, a, 194, a + 95))


def _crisp(src, size, pad=1):
    """96px arcade art -> crisp `size` px sprite.

    A plain LANCZOS downscale leaves the 3px black outline as a semi-transparent
    grey fringe, which is what looked blurry. Instead: hard-threshold the alpha,
    sharpen, flatten to a small palette, then draw a 1px dark contour so
    neighbouring bubbles stay separated.
    """
    inner = size - 2 * pad
    sm = src.resize((inner, inner), Image.LANCZOS)

    r, g, b, a = sm.split()
    a = a.point(lambda v: 0 if v < 128 else 255)   # hard silhouette, no faint fringe
    rgb = Image.merge("RGB", (r, g, b)).filter(
        ImageFilter.UnsharpMask(radius=0.9, percent=170, threshold=1))
    rgb = rgb.point(lambda v: (v // 17) * 17)
    rgb.putalpha(a)

    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(rgb, (pad, pad), rgb)

    px = out.load()
    for y in range(1, size - 1):
        for x in range(1, size - 1):
            if px[x, y][3] > 128:
                continue
            if any(px[x + dx, y + dy][3] > 128
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                px[x, y] = (24, 20, 32, 235)
    return out


def _add_sweep(im, t):
    """Diagonal specular sweep, masked to the existing alpha."""
    out = im.copy()
    p = out.load()
    w, h = out.size
    cx = cy = (w - 1) / 2.0
    R = w * 0.5 - 2.0
    for y in range(h):
        for x in range(w):
            r, g, b, a = p[x, y]
            if a < 8:
                continue
            dx = (x - cx) / R
            dy = (y - cy) / R
            if dx * dx + dy * dy > 1.05:
                continue
            proj = dx * 0.7071 + dy * 0.7071
            pos = -1.45 + 2.95 * t
            band = math.exp(-((proj - pos) ** 2) / 0.018) * 0.95
            p[x, y] = (min(255, int(r + 238 * band)),
                       min(255, int(g + 242 * band)),
                       min(255, int(b + 248 * band)), a)
    return out


def make_bubbles():
    """Procedural glass bubbles filled with FSM tile textures.

    (The arcade sheet was tried here, but its sprite is authored on a square
    canvas with a black contour: at a 40px hex pitch the contour lands on the
    neighbour. The generated sphere has a circular silhouette instead.)
    """
    out = []
    for i, (sheet, c, r, col) in enumerate(BUBBLES):
        tile = pa.load_cell(sheet, c, r)
        out.append(("bubble%d" % i, bubble_at_size(tile, col, 40)))
        out.append(("bubble%d_big" % i, bubble_at_size(tile, col, 64)))
        # 16-frame strip: 0..5 = specular sweep, 6..15 = idle
        strip = Image.new("RGBA", (40 * 16, 40), (0, 0, 0, 0))
        for f in range(16):
            sh = (f / 6.0) if f < 6 else None
            strip.paste(bubble_at_size(tile, col, 40, shine=sh), (f * 40, 0))
        out.append(("bubble%d_anim" % i, strip))
    return out


def make_background():
    bg = Image.open(os.path.join(PARA, "ForestNight.png")).convert("RGB")
    bg = cover(bg, WIN_W, WIN_H)
    bg = bg.filter(ImageFilter.GaussianBlur(2.2))
    bg = Image.eval(bg, lambda v: int(v * 0.66))
    bg = Image.blend(bg, Image.new("RGB", (WIN_W, WIN_H), (10, 14, 26)), 0.28)
    return ("bg", bg.convert("RGBA"))


def make_cannon_base(size=64):
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = size / 2.0
    for rr in range(int(c), 0, -1):
        t = rr / c
        col = (int(30 + 46 * t), int(36 + 50 * t), int(48 + 58 * t), 255)
        d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=col)
    d.ellipse([2, 2, size - 3, size - 3], outline=(118, 138, 168, 255), width=2)
    d.ellipse([7, 7, size - 8, size - 8], outline=(64, 78, 100, 255), width=1)
    for a in range(0, 360, 45):
        x = c + math.cos(math.radians(a)) * 23
        y = c + math.sin(math.radians(a)) * 23
        d.ellipse([x - 2.2, y - 2.2, x + 2.2, y + 2.2], fill=(158, 172, 196, 255))
    d.ellipse([c - 13, c - 13, c + 13, c + 13], fill=(20, 25, 35, 255),
              outline=(96, 112, 138, 255), width=2)
    return ("cannon_base", im)


def make_cannon_barrel(w=20, h=48):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    for y in range(h):
        for x in range(w):
            t = (x - w / 2.0) / (w / 2.0)
            base = 148.0 - 34.0 * abs(t) - 16.0 * (y / h)
            grain = 10.0 * math.sin(y * 0.62 + x * 1.9) + 5.0 * math.sin(y * 2.1)
            r = base + grain
            g = base * 0.70 + grain * 0.55
            b = base * 0.38
            if x < 1 or x > w - 2:
                r, g, b = r * 0.45, g * 0.45, b * 0.45
            px[x, y] = (max(0, min(255, int(r))), max(0, min(255, int(g))),
                        max(0, min(255, int(b))), 255)
    d = ImageDraw.Draw(im)
    d.rectangle([1, 8, w - 2, 12], fill=(96, 102, 114, 255))
    d.rectangle([1, h - 13, w - 2, h - 9], fill=(96, 102, 114, 255))
    d.rectangle([0, 0, w - 1, h - 1], outline=(28, 20, 14, 255))
    d.ellipse([2, 0, w - 3, 7], fill=(58, 42, 30, 255), outline=(24, 18, 12, 255))
    return ("cannon_barrel", im)


def make_dot(size=16):
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = im.load()
    c = size / 2.0
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - c + 0.5, y - c + 0.5) / c
            a = max(0.0, 1.0 - d)
            a = a * a
            px[x, y] = (255, 255, 255, int(255 * a))
    return ("dot", im)


def make_ring(size=64):
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = im.load()
    c = size / 2.0
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - c + 0.5, y - c + 0.5) / c
            band = math.exp(-((d - 0.82) ** 2) / 0.006)
            a = max(0.0, min(1.0, band))
            px[x, y] = (255, 255, 255, int(255 * a))
    return ("ring", im)


def make_wave(w=160, h=64):
    """Soft vertical light band, drawn additively and stretched across the field."""
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    c = (w - 1) / 2.0
    for y in range(h):
        for x in range(w):
            u = (x - c) / c
            a = math.exp(-(u * u) / 0.22)
            px[x, y] = (255, 255, 255, int(255 * a))
    return ("wave", im)


def make_font():
    path = None
    for name in ("consola.ttf", "consolab.ttf", "arial.ttf"):
        p = os.path.join(r"C:\Windows\Fonts", name)
        if os.path.exists(p):
            path = p
            break
    f = ImageFont.truetype(path, 18) if path else ImageFont.load_default()
    W = FONT_CW * FONT_COLS
    H = FONT_CH * 6
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for i in range(96):
        ch = chr(32 + i)
        cx = (i % FONT_COLS) * FONT_CW
        cy = (i // FONT_COLS) * FONT_CH
        d.text((cx + 1, cy + 3), ch, font=f, fill=(255, 255, 255, 255))
    return ("font", im)


def write_pak(path, images):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    blobs = [(n, to_bgra(im)) for n, im in images]
    with open(path, "wb") as fp:
        fp.write(b"HGEP")
        fp.write(struct.pack("<II", 1, len(blobs)))
        for n, im in blobs:
            w, h = im.size
            nm = n.encode("ascii")[:23].ljust(24, b"\0")
            fp.write(nm)
            fp.write(struct.pack("<III", w, h, w * h * 4))
        total = 0
        for n, im in blobs:
            data = im.tobytes()
            assert len(data) == im.size[0] * im.size[1] * 4
            fp.write(data)
            total += len(data)
            print("  %-14s %4dx%-4d %7d bytes" % (n, im.size[0], im.size[1], len(data)))
        print("  total payload %.2f MB" % (total / 1048576.0))


def make_bub():
    """Green + blue dinosaur poses, extracted + keyed from the arcade sheet.

    NOTE: placeholder art ripped from Bust-a-Move (Taito). Internal use only.
    """
    import extract_bub as E
    src = Image.open(E.SRC).convert("RGB")
    out = []
    sel = [(0, "idle0"), (1, "idle1"), (2, "idle2"), (7, "shoot"),
           (14, "cheer"), (6, "sad"), (9, "look")]

    for v, (y0, y1) in enumerate([(366, 404), (415, 453)]):
        runs = E.runs_for(src, y0, y1)
        frames = [E.key_box(src, (a, y0, b, y1)) for a, b in runs]

        def canvas(fr):
            im = Image.new("RGBA", (40, 40), (0, 0, 0, 0))
            im.paste(fr, ((40 - fr.width) // 2, 40 - fr.height), fr)
            return im

        for i, nm in sel:
            out.append(("bub%d_%s" % (v, nm), canvas(frames[i])))
    return out


def main():
    imgs = []
    imgs += make_bubbles()
    imgs.append(make_background())
    imgs.append(make_cannon_base())
    imgs.append(make_cannon_barrel())
    imgs.append(make_dot())
    imgs.append(make_ring())
    imgs.append(make_wave())
    imgs += make_bub()
    imgs.append(make_font())
    write_pak(OUT_PAK, imgs)
    print("wrote", OUT_PAK, "%.2f MB" % (os.path.getsize(OUT_PAK) / 1048576.0))


if __name__ == "__main__":
    main()
