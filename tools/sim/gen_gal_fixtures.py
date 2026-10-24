#!/usr/bin/env python3
"""Fotos de prueba de la Galería (deterministas: mismas semillas, mismos bytes).

  python3 tools/sim/gen_gal_fixtures.py            -> las pequeñas que se versionan
  python3 tools/sim/gen_gal_fixtures.py --big DIR  -> las grandes de la prueba de
                                                      extremo a extremo (no se versionan)

Pequeñas (tests/host/fixtures y tools/sim/fixtures):
  gal_quad_640x480.jpg   cuatro cuadrantes rojo, verde, azul y blanco: orden de
                         los canales, recorte de la miniatura y encaje del visor
  gal_tall_300x900.jpg   vertical
  gal_tiny_40x30.jpg     más pequeña que la miniatura (ampliación)
  gal_prog_200x150.jpg   JPEG progresivo (el reloj no lo decodifica)
  gal_scene_N.jpg        escenas para la vista previa (800x600 / 600x800)
Grandes (--big): 4000x3000 (~2-3 MB, varias partes de 256 KB) y 1600x1200.
Requiere Pillow.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.abspath(__file__))
HOST_FIX = os.path.normpath(os.path.join(ROOT, "..", "..", "tests", "host", "fixtures"))
SIM_FIX = os.path.join(ROOT, "fixtures")


def save(img, path, **kw):
    img.save(path, "JPEG", optimize=False, **kw)
    print(f"{path}: {os.path.getsize(path)} B")


def quad(w, h):
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, w // 2 - 1, h // 2 - 1], fill=(255, 0, 0))
    d.rectangle([w // 2, 0, w - 1, h // 2 - 1], fill=(0, 255, 0))
    d.rectangle([0, h // 2, w // 2 - 1, h - 1], fill=(0, 0, 255))
    d.rectangle([w // 2, h // 2, w - 1, h - 1], fill=(255, 255, 255))
    return im


def scene(w, h, seed):
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h))
    px = im.load()
    top = (rnd.randint(20, 90), rnd.randint(60, 140), rnd.randint(150, 230))
    bot = (rnd.randint(150, 240), rnd.randint(100, 180), rnd.randint(60, 140))
    for y in range(h):
        t = y / (h - 1)
        c = tuple(int(top[i] * (1 - t) + bot[i] * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    d = ImageDraw.Draw(im)
    sx, sy, r = rnd.randint(w // 5, 4 * w // 5), rnd.randint(h // 8, h // 3), min(w, h) // 9
    d.ellipse([sx - r, sy - r, sx + r, sy + r], fill=(255, 226, 150))
    for k in range(3):  # montañas
        base = h * (0.55 + 0.12 * k)
        col = tuple(max(0, int(v * (0.55 - 0.12 * k))) for v in bot)
        pts = [(0, h)]
        for x in range(0, w + 40, 40):
            pts.append((x, base - rnd.randint(0, h // 5) * (1 - k * 0.25)))
        pts.append((w, h))
        d.polygon(pts, fill=col)
    return im.filter(ImageFilter.GaussianBlur(1.2))


def noisy(w, h, seed):
    """Grande y con detalle (no se comprime a casi nada): varias partes de subida."""
    rnd = random.Random(seed)
    im = scene(w // 4, h // 4, seed).resize((w, h), Image.BICUBIC)
    px = im.load()
    for _ in range(w * h // 40):
        x, y = rnd.randrange(w), rnd.randrange(h)
        r, g, b = px[x, y]
        n = rnd.randint(-40, 40)
        px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n)), max(0, min(255, b + n)))
    return im


def small():
    os.makedirs(HOST_FIX, exist_ok=True)
    save(quad(640, 480), os.path.join(HOST_FIX, "gal_quad_640x480.jpg"), quality=92)
    save(scene(300, 900, 7), os.path.join(HOST_FIX, "gal_tall_300x900.jpg"), quality=80)
    save(quad(40, 30), os.path.join(HOST_FIX, "gal_tiny_40x30.jpg"), quality=95)
    save(scene(200, 150, 3), os.path.join(HOST_FIX, "gal_prog_200x150.jpg"), quality=80, progressive=True)
    for i, (w, h) in enumerate([(800, 600), (600, 800), (800, 600), (800, 600)]):
        save(scene(w, h, 100 + i), os.path.join(SIM_FIX, f"gal_scene_{i}.jpg"), quality=78)


def big(out):
    os.makedirs(out, exist_ok=True)
    save(noisy(4000, 3000, 11), os.path.join(out, "gal_big_4000x3000.jpg"), quality=90)
    save(noisy(1600, 1200, 12), os.path.join(out, "gal_mid_1600x1200.jpg"), quality=88)
    save(quad(640, 480), os.path.join(out, "gal_quad_640x480.jpg"), quality=92)
    Image.new("RGB", (64, 64), (10, 200, 30)).save(os.path.join(out, "gal_png_64.png"))


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--big":
        big(sys.argv[2])
    else:
        small()
