#!/usr/bin/env python3
"""
Genera los fondos de las esferas fotográficas (assets/faces/*.jpg) a partir de
los diseños del usuario (design/faces/*.png, en la raíz del repositorio).

Cada diseño es una captura con la hora, la fecha y otros datos ya pintados.
En el reloj esos datos se dibujan con LVGL (son reales), así que aquí:
  1. se escala la imagen a 410x502 cubriendo la pantalla y se recorta al centro
     (sin deformar; se pierden como mucho 15 px por lado);
  2. se borran los textos dinámicos (máscara de sus píxeles claros dentro de su
     caja medida, ampliada 3 px) y se reconstruye el fondo (OpenCV, Telea). La
     hora nueva se dibuja encima, en el mismo sitio y con el mismo tamaño;
  3. la esfera analógica no se rellena: se reconstruye el degradado (desenfoque
     normalizado sin las marcas, con el corte nítido del lado iluminado) y se
     vuelven a dibujar las marcas y los números con su geometría medida. Las
     agujas, la fecha y la complicación de batería son de LVGL;
  4. se guarda en JPEG baseline (TJpgDec no admite progresivo), calidad 90.

Uso (desde "Flex OS Ultra Watch/"): python3 tools/faces/gen_faces.py
Requiere: Pillow, numpy, opencv-python-headless. La salida es determinista.
"""
import math
import os
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.normpath(os.path.join(PROJ, "..", "design", "faces"))
OUT = os.path.join(PROJ, "assets", "faces")
FONT = os.path.join(PROJ, "assets", "fonts", "Outfit-Regular.ttf")
W, H = 410, 502

# Cajas (x0, y0, x1, y1) de los textos dinámicos en la imagen ya escalada a 410x502.
TEXT = {
    "montana": [(257, 29, 371, 59), (115, 327, 372, 463)],
    "gato": [(303, 49, 396, 74), (176, 89, 395, 163)],
    "tierra": [(131, 13, 280, 47), (75, 73, 337, 146), (16, 470, 398, 495)],
    "flores": [(265, 17, 382, 40), (257, 62, 385, 236), (257, 247, 385, 421), (315, 445, 382, 469)],
    "taj": [(144, 21, 265, 53), (98, 75, 307, 185)],
}


def cover(im):
    s = max(W / im.width, H / im.height)
    nw, nh = round(im.width * s), round(im.height * s)
    r = im.resize((nw, nh), Image.LANCZOS)
    x0, y0 = (nw - W) // 2, (nh - H) // 2
    return r.crop((x0, y0, x0 + W, y0 + H))


def photo(name):
    im = cover(Image.open(os.path.join(SRC, name + ".png")).convert("RGB"))
    bgr = cv2.cvtColor(np.asarray(im), cv2.COLOR_RGB2BGR)
    g = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    m = np.zeros(g.shape, np.uint8)
    for (x0, y0, x1, y1) in TEXT[name]:
        x0, y0, x1, y1 = x0 - 4, y0 - 4, x1 + 4, y1 + 4
        m[y0:y1 + 1, x0:x1 + 1] = (g[y0:y1 + 1, x0:x1 + 1] > 150).astype(np.uint8) * 255
    m = cv2.dilate(m, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7)))
    out = cv2.inpaint(bgr, m, 7, cv2.INPAINT_TELEA)
    return Image.fromarray(cv2.cvtColor(out, cv2.COLOR_BGR2RGB))


def wblur(img, w, s):
    """Desenfoque normalizado por la máscara w; sin muestras cerca, escalas mayores."""
    out = acc = None
    for k in (s, s * 2.5, s * 6):
        num = cv2.GaussianBlur(img * w[..., None], (0, 0), k)
        den = cv2.GaussianBlur(w, (0, 0), k)[..., None]
        v = num / np.maximum(den, 1e-6)
        c = np.clip(den * 4, 0, 1)
        if out is None:
            out, acc = v * c, c
        else:
            out, acc = out + v * c * (1 - acc), acc + c * (1 - acc)
    return out / np.maximum(acc, 1e-6)


def rr_hit(cx, cy, th, ix, iy, rad):
    """Distancia desde el centro hasta el rectángulo redondeado (inset ix, iy) en la dirección th."""
    dx, dy = math.sin(th), -math.cos(th)
    lo, hi = 0.0, 400.0
    x0, y0, x1, y1 = ix, iy, W - 1 - ix, H - 1 - iy
    for _ in range(40):
        mid = (lo + hi) / 2
        x, y = cx + dx * mid, cy + dy * mid
        qx = max(x0 + rad - x, 0, x - (x1 - rad))
        qy = max(y0 + rad - y, 0, y - (y1 - rad))
        if x0 <= x <= x1 and y0 <= y <= y1 and qx * qx + qy * qy <= rad * rad:
            lo = mid
        else:
            hi = mid
    return lo


ANALOG_CX, ANALOG_CY = 207.5, 254.0  # centro de la esfera medido en la referencia


def analog():
    im = Image.open(os.path.join(SRC, "analogica.png")).convert("RGB")
    im = im.crop((12, 8, 811, 985)).resize((W, H), Image.LANCZOS)  # pantalla, sin el bisel
    src = np.asarray(im).astype(np.float32)
    cx, cy = ANALOG_CX, ANALOG_CY
    g = cv2.cvtColor(src.astype(np.uint8), cv2.COLOR_RGB2GRAY).astype(np.float32)
    el = ((g - cv2.GaussianBlur(g, (0, 0), 5)) > 10).astype(np.uint8)
    m = cv2.dilate(el, np.ones((5, 5), np.uint8))
    mm = np.zeros((H, W), np.uint8)
    c = (int(cx), int(cy))
    cv2.line(mm, c, (118, 192), 1, 28)    # aguja horaria de la captura
    cv2.line(mm, c, (362, 154), 1, 28)    # minutero
    cv2.circle(mm, c, 16, 1, -1)
    cv2.rectangle(mm, (150, 140), (262, 180), 1, -1)  # fecha
    cv2.circle(mm, (200, 328), 46, 1, -1)              # complicación de batería
    cv2.rectangle(mm, (200, 215), (214, H - 1), 1, -1)  # segundero
    hsv = cv2.cvtColor(src.astype(np.uint8), cv2.COLOR_RGB2HSV)
    bezel = ((hsv[..., 1] < 45) & (hsv[..., 2] > 80)).astype(np.uint8)
    yy, xx = np.mgrid[0:H, 0:W]
    edge = (xx < 6) | (xx > W - 7) | (yy < 6) | (yy > H - 7)
    valid = ((m == 0) & (mm == 0) & (bezel == 0) & ~edge).astype(np.float32)
    split_x, split_y = 207, 212  # corte nítido del lado iluminado (bajo el centro)
    full = wblur(src, valid, 9)
    left = wblur(src, valid * (xx < split_x - 2), 9)
    right = wblur(src, valid * (xx > split_x + 3), 9)
    t = np.clip((yy - split_y) / 14.0, 0, 1)[..., None]
    side = np.where((xx < split_x)[..., None], left, right)
    bg = np.where((yy >= split_y)[..., None], full * (1 - t) + side * t, full)

    S = 4  # supermuestreo de las marcas y los números
    lay = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    ink = (225, 232, 242)
    for k in range(12):  # marcas horarias: radiales, 80 px, el extremo sobre el contorno
        th = math.radians(30 * k)
        r1 = rr_hit(cx, cy, th, 30, 36, 70)
        r0 = r1 - 80
        p = [(cx + math.sin(th) * r, cy - math.cos(th) * r) for r in (r0, r1)]
        d.line([(x * S, y * S) for x, y in p], fill=ink + (200,), width=int(2.6 * S))
    for k in range(60):  # minutos: 11 px en el borde
        if k % 5 == 0:
            continue
        th = math.radians(6 * k)
        r1 = rr_hit(cx, cy, th, 2, 2, 78)
        p = [(cx + math.sin(th) * r, cy - math.cos(th) * r) for r in (r1 - 11, r1)]
        d.line([(x * S, y * S) for x, y in p], fill=ink + (120,), width=int(1.4 * S))
    font = ImageFont.truetype(FONT, 17 * S)
    for k in range(12):
        th = math.radians(30 * k)
        r = rr_hit(cx, cy, th, 15, 15, 66)
        x, y = cx + math.sin(th) * r, cy - math.cos(th) * r
        d.text((x * S, y * S), "%02d" % (60 if k == 0 else 5 * k), font=font, fill=ink + (150,), anchor="mm")
    la = np.asarray(lay.resize((W, H), Image.LANCZOS)).astype(np.float32) / 255
    out = bg * (1 - la[..., 3:]) + la[..., :3] * 255 * la[..., 3:]
    R = 58  # fuera del contorno redondeado de la pantalla: negro
    for (ccx, ccy) in [(R, R), (W - 1 - R, R), (R, H - 1 - R), (W - 1 - R, H - 1 - R)]:
        qx = (xx < R) if ccx == R else (xx > W - 1 - R)
        qy = (yy < R) if ccy == R else (yy > H - 1 - R)
        out[qx & qy & (((xx - ccx) ** 2 + (yy - ccy) ** 2) > R * R)] = 0
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def main():
    os.makedirs(OUT, exist_ok=True)
    jobs = [(n, photo) for n in TEXT] + [("analogica", None)]
    for name, fn in jobs:
        im = analog() if fn is None else fn(name)
        path = os.path.join(OUT, "face_%s.jpg" % name)
        # 4:4:4 en la analógica (líneas finas); 4:2:0 en las fotografías
        im.save(path, "JPEG", quality=90, optimize=True, progressive=False,
                subsampling=0 if fn is None else 2)
        print("%-10s %6d B  %s" % (name, os.path.getsize(path), path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
