#!/usr/bin/env python3
"""Referencia del usuario (design/referencias) | render de la vista previa, lado a lado.
Uso: python3 tools/sim/compare.py <carpeta de la vista previa> <salida>
Requiere Pillow (solo para revisar; no lo usa la compilación)."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / "design" / "referencias"
PAIRS = [
    ("reloj_hora", "rd_reloj_hora"), ("reloj_cronometro", "rd_reloj_cronometro"),
    ("reloj_alarmas", "rd_reloj_alarmas"), ("reloj_temporizador", "rd_reloj_temporizador"),
    ("almacenamiento", "rd_almacenamiento"), ("panel_rapido", "f3_panel"), ("brujula", "rd_brujula"),
    ("editor_esferas", "f10_selector_final"),
]
# Esferas: la captura original (design/faces) recortada como en tools/faces/gen_faces.py
FACES = [("montana", "rd_esfera_montana"), ("analogica", "rd_esfera_ultra"), ("gato", "rd_esfera_luna"),
         ("tierra", "rd_esfera_tierra"), ("flores", "rd_esfera_flores"), ("taj", "rd_esfera_taj")]


def cover(im):
    s = max(410 / im.width, 502 / im.height)
    nw, nh = round(im.width * s), round(im.height * s)
    r = im.resize((nw, nh))
    x0, y0 = (nw - 410) // 2, (nh - 502) // 2
    return r.crop((x0, y0, x0 + 410, y0 + 502))


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    dst.mkdir(parents=True, exist_ok=True)
    for ref, ren in PAIRS:
        r, p = REF / (ref + ".png"), src / (ren + ".png")
        if not r.exists() or not p.exists():
            continue
        out = Image.new("RGB", (410 * 2 + 12, 502 + 26), (40, 40, 40))
        out.paste(Image.open(r).convert("RGB").resize((410, 502)), (0, 26))
        out.paste(Image.open(p).convert("RGB"), (422, 26))
        d = ImageDraw.Draw(out)
        d.text((6, 6), "referencia: " + ref, fill=(255, 255, 0))
        d.text((428, 6), "vista previa: " + ren, fill=(0, 255, 255))
        out.save(dst / ("cmp_" + ref + ".png"))
        print("comparación:", dst / ("cmp_" + ref + ".png"))
    for ref, ren in FACES:
        r, p = ROOT / "design" / "faces" / (ref + ".png"), src / (ren + ".png")
        if not r.exists() or not p.exists():
            continue
        im = Image.open(r).convert("RGB")
        if ref == "analogica":
            im = im.crop((12, 8, 811, 985)).resize((410, 502))
        out = Image.new("RGB", (410 * 2 + 12, 502 + 26), (40, 40, 40))
        out.paste(cover(im), (0, 26))
        out.paste(Image.open(p).convert("RGB"), (422, 26))
        d = ImageDraw.Draw(out)
        d.text((6, 6), "referencia: " + ref, fill=(255, 255, 0))
        d.text((428, 6), "vista previa: " + ren, fill=(0, 255, 255))
        out.save(dst / ("cmp_esfera_" + ref + ".png"))


if __name__ == "__main__":
    main()
