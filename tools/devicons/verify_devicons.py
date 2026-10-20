#!/usr/bin/env python3
"""
verify_devicons.py — comprueba que el VectorDrawable de Android dibuja LO MISMO
que el SVG fuente: reconstruye un SVG con los pathData del XML, rasteriza los dos
con Chromium a 192 px y compara pixel a pixel. Deja la comparacion en PNG.

Uso: python3 tools/devicons/verify_devicons.py <carpeta_de_salida>
"""
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
from PIL import Image, ImageChops

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
A = '{http://schemas.android.com/apk/res/android}'
OUT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/devicons'
os.makedirs(OUT, exist_ok=True)
SIZE = 192


def vd_to_svg(xml_path):
    root = ET.parse(xml_path).getroot()
    vw, vh = root.get(A + 'viewportWidth'), root.get(A + 'viewportHeight')
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %s %s" width="96" height="96">' % (vw, vh)]
    for p in root.findall('path'):
        a = {'d': p.get(A + 'pathData'), 'fill': p.get(A + 'fillColor', 'none')}
        if p.get(A + 'strokeColor'):
            a['stroke'] = p.get(A + 'strokeColor')
            a['stroke-width'] = p.get(A + 'strokeWidth', '1')
            for k, s in (('strokeLineCap', 'stroke-linecap'), ('strokeLineJoin', 'stroke-linejoin')):
                if p.get(A + k):
                    a[s] = p.get(A + k)
        parts.append('<path ' + ' '.join('%s="%s"' % kv for kv in a.items()) + '/>')
    parts.append('</svg>')
    return '\n'.join(parts)


def raster(svg_path, png):
    subprocess.run(['node', os.path.join(ROOT, 'tools', 'devicons', 'rasterize.mjs'), svg_path, png, str(SIZE)],
                   check=True, env=dict(os.environ, CHROMIUM='/opt/pw-browsers/chromium'))
    return Image.open(png).convert('RGBA')


worst = 0
sheet = Image.new('RGBA', (SIZE * 3, SIZE * 4), (24, 26, 34, 255))
for row, n in enumerate(['watch_ultra', 'ultra_p4', 'phone', 'unknown']):
    src = os.path.join(ROOT, 'design', 'devices', n + '.svg')
    vd = os.path.join(ROOT, 'FlexPhone', 'app', 'src', 'main', 'res', 'drawable', 'ic_dev_%s.xml' % n)
    rebuilt = os.path.join(OUT, n + '_vd.svg')
    with open(rebuilt, 'w') as f:
        f.write(vd_to_svg(vd))
    a = raster(src, os.path.join(OUT, n + '_svg.png'))
    b = raster(rebuilt, os.path.join(OUT, n + '_vd.png'))
    diff = ImageChops.difference(a, b)
    raw = diff.tobytes()
    bad = sum(1 for i in range(0, len(raw), 4) if max(raw[i:i + 4]) > 24)
    pct = 100.0 * bad / (len(raw) // 4)
    worst = max(worst, pct)
    print('%-12s pixeles distintos: %.2f %%' % (n, pct))
    sheet.alpha_composite(a, (0, row * SIZE))
    sheet.alpha_composite(b, (SIZE, row * SIZE))
    sheet.alpha_composite(diff.point(lambda v: min(255, v * 4)), (2 * SIZE, row * SIZE))
sheet.save(os.path.join(OUT, 'comparacion.png'))
print('peor: %.2f %%  ->  %s' % (worst, os.path.join(OUT, 'comparacion.png')))
sys.exit(0 if worst < 0.5 else 1)
