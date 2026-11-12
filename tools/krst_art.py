#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""KRIECHSTROM's pictures, drawn here (no stock art). One image: the Lichtenberg figure, the tree a creeping current
burns into old insulation, and into the skin of someone the lightning did not kill: a scar of what went through.
Red glow, a white core, dark scar tissue around it, on a worn ground (grain, dust, scratches, a burnt vignette).

  site/art/hero.jpg        1600 x 800   the landing page: the scar crossing the frame
  site/art/tracking.jpg    1200 x 1200  the figure alone, from its centre
  site/art/band.jpg        1600 x 360   a strip: the scar along it (the manual's head, the pages' foot)
  firmware/src/krst_art.h  the splash picture, 240 x 150, 4 levels (BG, dim red, red, white), run-length coded

  python3 tools/krst_art.py [--seed N]
"""
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
RED = (255, 32, 24)
BLOOD = (120, 6, 4)
WHITE = (255, 236, 228)
BLACK = (0, 0, 0)


def ground(w, h, seed):
    """a worn dark ground: low mottling (skin, old paint), grain, dust, vertical scratches, a burnt vignette"""
    rnd = random.Random(seed)
    small = Image.new("L", (max(8, w // 40), max(8, h // 40)))
    small.putdata([rnd.randint(0, 255) for _ in range(small.size[0] * small.size[1])])
    mott = small.resize((w, h), Image.BICUBIC).filter(ImageFilter.GaussianBlur(w / 120))
    grain = Image.effect_noise((w, h), 48)
    base = Image.merge("RGB", (Image.eval(mott, lambda v: int(14 + v * 0.10)), Image.eval(mott, lambda v: int(v * 0.02)),
                               Image.eval(mott, lambda v: int(v * 0.018))))
    base = ImageChops.add(base, Image.merge("RGB", [Image.eval(grain, lambda v: max(0, v - 118) // 4)] * 3))
    d = ImageDraw.Draw(base)
    for _ in range(int(w * h / 9000)):                     # dust
        x, y, r = rnd.uniform(0, w), rnd.uniform(0, h), rnd.uniform(0.4, 1.8)
        c = rnd.randint(30, 80)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(c, c // 3, c // 3))
    for _ in range(int(w / 40)):                           # scratches down the film
        x, y0 = rnd.uniform(0, w), rnd.uniform(-h * 0.2, h * 0.6)
        y1 = y0 + rnd.uniform(h * 0.2, h * 1.1)
        c = rnd.randint(28, 70)
        amp = rnd.uniform(0, 1.5)
        pts = [(x + math.sin(t / 7.0) * amp, y0 + (y1 - y0) * t / 40) for t in range(41)]
        d.line(pts, fill=(c, c // 2, c // 2), width=1)
    vig = Image.new("L", (w, h), 0)
    vd = ImageDraw.Draw(vig)
    for i in range(40):
        t = i / 40
        vd.ellipse([w * (0.5 - 0.75 + t * 0.3), h * (0.5 - 0.85 + t * 0.35), w * (0.5 + 0.75 - t * 0.3),
                    h * (0.5 + 0.85 - t * 0.35)], fill=int(255 * t))
    vig = vig.filter(ImageFilter.GaussianBlur(w / 30))
    return Image.composite(base, Image.new("RGB", (w, h), BLACK), vig)


def tree(draw, x, y, angle, length, width, depth, rnd, spread=0.42):
    """a branching discharge from (x, y): many short wandering steps, a branch now and then, thinning to a hair"""
    if depth <= 0 or width < 0.35:
        return
    head = angle
    for _ in range(rnd.randint(7, 13)):
        head += rnd.gauss(0, spread)
        head = head * 0.85 + angle * 0.15
        l = length * rnd.uniform(0.15, 0.4)
        nx, ny = x + math.cos(head) * l, y + math.sin(head) * l
        draw.line([(x, y), (nx, ny)], fill=255, width=max(1, int(round(width))))
        if rnd.random() < 0.3:
            tree(draw, nx, ny, head + rnd.choice((-1, 1)) * rnd.uniform(0.4, 1.1), length * 0.72, width * 0.55,
                 depth - 1, rnd, spread)
        x, y = nx, ny
        width *= 0.9


def scar(w, h, seeds, rnd):
    """the figure's mask (L, 0..255) at 2x and down: seeds = [(x, y, angle, length, width, arms)]"""
    W, H = w * 2, h * 2
    m = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(m)
    for x, y, ang, ln, wd, arms in seeds:
        for k in range(arms):
            step = 2 * math.pi / arms if arms > 3 else 0.5
            a = ang + (k - (arms - 1) / 2) * step + rnd.uniform(-0.2, 0.2)
            tree(d, x * 2, y * 2, a, ln * 2, wd * 2, 8, rnd)
    return m.resize((w, h), Image.LANCZOS)


def burn(im, mask, scale):
    """the scar on the ground: dark tissue around it, a red glow, a white-hot core"""
    w, h = im.size
    tissue = mask.filter(ImageFilter.GaussianBlur(scale * 3))
    im = Image.composite(Image.new("RGB", (w, h), (8, 0, 0)), im, Image.eval(tissue, lambda v: min(255, v * 3)))
    halo = Image.eval(mask.filter(ImageFilter.GaussianBlur(scale * 1.6)), lambda v: min(255, int(v * 2.6)))
    im = Image.composite(Image.new("RGB", (w, h), BLOOD), im, halo)
    glow = Image.eval(mask.filter(ImageFilter.GaussianBlur(scale * 0.6)), lambda v: min(255, int(v * 2.2)))
    im = Image.composite(Image.new("RGB", (w, h), RED), im, glow)
    core = Image.eval(mask, lambda v: 255 if v > 150 else int(v * 1.2))
    return Image.composite(Image.new("RGB", (w, h), WHITE), im, core)


def scanlines(im, step=3, dark=0.78):
    w, h = im.size
    m = Image.new("L", (w, h), 255)
    d = ImageDraw.Draw(m)
    for y in range(0, h, step):
        d.line([(0, y), (w, y)], fill=int(255 * dark))
    return ImageChops.multiply(im, Image.merge("RGB", [m] * 3))


def hero(seed):
    rnd = random.Random(seed)
    w, h = 1600, 800
    m = scar(w, h, [(1120, 380, 0, 95, 7, 7)], rnd)
    return scanlines(burn(ground(w, h, seed), m, 4))


def tracking(seed):
    rnd = random.Random(seed + 2)
    s = 1200
    m = scar(s, s, [(s / 2, s / 2, 0, 110, 8, 8)], rnd)
    return scanlines(burn(ground(s, s, seed + 2), m, 4), 4)


def band(seed):
    rnd = random.Random(seed + 3)
    w, h = 1600, 360
    m = scar(w, h, [(-10, h * 0.55, 0, 150, 5, 1), (w + 10, h * 0.45, math.pi, 150, 5, 1)], rnd)
    return scanlines(burn(ground(w, h, seed + 3), m, 3))


def splash(seed):
    """240 x 150: the figure from its centre, big, for the small screen (thicker lines)"""
    rnd = random.Random(seed + 4)
    w, h = 960, 600
    m = scar(w, h, [(480, 300, 0, 78, 11, 6)], rnd)
    im = burn(Image.new("RGB", (w, h), BLACK), m, 5)
    return im.resize((240, 150), Image.LANCZOS)


def to_levels(im):
    """4 levels for the device: 0 BG, 1 dim red, 2 red, 3 white"""
    w, h = im.size
    px = im.load()
    out = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            out.append(3 if g > 140 and r > 200 else 2 if r > 170 else 1 if r > 70 else 0)
    return out


def rle(levels):
    """(level << 6) | (run - 1), runs of 1 .. 64"""
    out, i = [], 0
    while i < len(levels):
        v, n = levels[i], 1
        while i + n < len(levels) and levels[i + n] == v and n < 64:
            n += 1
        out.append(v << 6 | (n - 1))
        i += n
    return out


def main():
    seed = 1781
    if "--seed" in sys.argv:
        seed = int(sys.argv[sys.argv.index("--seed") + 1])
    art = ROOT / "site" / "art"
    art.mkdir(parents=True, exist_ok=True)
    (art / "plant.png").unlink(missing_ok=True)
    for old in ("hero.png", "tracking.png", "band.png"):
        (art / old).unlink(missing_ok=True)
    hero(seed).save(art / "hero.jpg", quality=84, optimize=True)
    tracking(seed).save(art / "tracking.jpg", quality=84, optimize=True)
    band(seed).save(art / "band.jpg", quality=84, optimize=True)
    small = splash(seed)
    small.save(art / "splash_preview.png")
    data = rle(to_levels(small))
    hdr = ROOT / "firmware" / "src" / "krst_art.h"
    rows = ["    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + "," for i in range(0, len(data), 16)]
    hdr.write_text(
        "/* SPDX-License-Identifier: GPL-3.0-only */\n"
        "/* KRIECHSTROM's splash picture (tools/krst_art.py: a Lichtenberg figure, the scar a creeping current leaves),\n"
        " * generated: 240 x 150, 4 levels (0 BG, 1 dim red, 2 red, 3 white) run-length coded: (level << 6) | (run - 1) */\n"
        f"#define KRST_ART_W 240\n#define KRST_ART_H 150\n#define KRST_ART_N {len(data)}u\n"
        "static const uint8_t KRST_ART[KRST_ART_N] = {\n" + "\n".join(rows) + "\n};\n", encoding="utf-8")
    print(f"art: hero, tracking, band in {art}; splash {len(data)} B -> {hdr}")


main()
