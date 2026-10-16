#!/usr/bin/env python3
"""Hoja de comparación de iconos: P4 (72 px, una muestra por píxel, ampliado al
mismo tamaño físico) frente al reloj (104 px con antialiasing), sobre negro.

Uso: contact_sheet.py <carpeta_reloj> <carpeta_p4> <salida.png>
Necesita Pillow. Herramienta de documentación: no forma parte del firmware.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

NAMES = ["reloj", "almacen", "notas", "nav", "brujula", "paint", "ajustes",
         "calc", "calend", "clima", "flexstore", "flexphone", "devcare", "musica", "sensores"]


def load(folder: Path, name: str) -> Image.Image:
    raw = (folder / f"app_{name}.rgba").read_bytes()
    side = int((len(raw) // 4) ** 0.5)
    return Image.frombytes("RGBA", (side, side), raw)


def main() -> None:
    watch, p4, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    cell, pad, cols = 104, 14, 7
    rows = (len(NAMES) + cols - 1) // cols
    W = pad + cols * (cell + pad)
    H = 30 + rows * 2 * (cell + 30)
    sheet = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(sheet)
    for i, n in enumerate(NAMES):
        r, c = divmod(i, cols)
        x = pad + c * (cell + pad)
        y = 30 + r * 2 * (cell + 30)
        ref = load(p4, n).resize((cell, cell), Image.NEAREST)
        sheet.paste(ref, (x, y), ref)
        w = load(watch, n)
        sheet.paste(w, (x, y + cell + 30), w)
        d.text((x, y + cell + 4), n + " P4", fill=(150, 150, 160))
        d.text((x, y + 2 * cell + 34), n + " reloj", fill=(150, 150, 160))
    d.text((pad, 8), "Arriba: P4 72 px (sin AA, ampliado)   Abajo: reloj 104 px (AA 8x8)", fill=(220, 220, 230))
    sheet.save(out)
    print("PNG:", out)


if __name__ == "__main__":
    main()
