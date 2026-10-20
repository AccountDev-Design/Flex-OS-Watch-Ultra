#!/usr/bin/env python3
"""
gen_devicons.py — los iconos de los dispositivos Flex, de UN SVG a cada plataforma.

  design/devices/<nombre>.svg  (fuente unica, viewBox 0 0 96 96)
     ├─> FlexPhone/app/src/main/res/drawable/ic_dev_<nombre>.xml
     │     VectorDrawable: Android lo pinta vectorial a cualquier densidad.
     └─> Flex OS Ultra Watch/components/watch_theme/flex_device_icons.c
           RGB565A8 prerrenderizado (Chromium) al tamano en que se usa. El
           ESP32-S3 no lleva motor SVG: rasterizar en el reloj costaria RAM
           interna y CPU en cada dibujo para un icono que nunca cambia.

El SVG admite lo que usan los iconos: rect (rx), circle y path (M L H V C Q A Z),
con fill, stroke, stroke-width, stroke-linecap y stroke-linejoin. Cualquier otra
cosa es un error, no un icono que sale distinto en cada plataforma.

Uso: python3 tools/devicons/gen_devicons.py   (desde la raiz del repositorio)
"""
import os
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'design', 'devices')
ANDROID = os.path.join(ROOT, 'FlexPhone', 'app', 'src', 'main', 'res', 'drawable')
WATCH_C = os.path.join(ROOT, 'Flex OS Ultra Watch', 'components', 'watch_theme', 'flex_device_icons.c')
WATCH_H = os.path.join(ROOT, 'Flex OS Ultra Watch', 'components', 'watch_theme', 'include', 'flex_device_icons.h')
NS = '{http://www.w3.org/2000/svg}'

# Que va al reloj y a que tamano (px). El reloj solo ensena el telefono.
WATCH_ICONS = [('phone', 56)]
ANDROID_ICONS = ['watch_ultra', 'ultra_p4', 'phone', 'unknown']


def fmt(v):
    s = ('%.3f' % float(v)).rstrip('0').rstrip('.')
    return s if s else '0'


def rect_path(e):
    x, y, w, h = (float(e.get(k, 0)) for k in ('x', 'y', 'width', 'height'))
    r = min(float(e.get('rx', 0)), w / 2, h / 2)
    if r <= 0:
        return 'M%s,%sH%sV%sH%sZ' % (fmt(x), fmt(y), fmt(x + w), fmt(y + h), fmt(x))
    a = 'A%s,%s 0 0 1 ' % (fmt(r), fmt(r))
    return ('M%s,%s' % (fmt(x + r), fmt(y)) + 'H%s' % fmt(x + w - r) + a + '%s,%s' % (fmt(x + w), fmt(y + r)) +
            'V%s' % fmt(y + h - r) + a + '%s,%s' % (fmt(x + w - r), fmt(y + h)) + 'H%s' % fmt(x + r) + a +
            '%s,%s' % (fmt(x), fmt(y + h - r)) + 'V%s' % fmt(y + r) + a + '%s,%sZ' % (fmt(x + r), fmt(y)))


def circle_path(e):
    cx, cy, r = (float(e.get(k, 0)) for k in ('cx', 'cy', 'r'))
    a = 'A%s,%s 0 1 0 ' % (fmt(r), fmt(r))
    return 'M%s,%s' % (fmt(cx - r), fmt(cy)) + a + '%s,%s' % (fmt(cx + r), fmt(cy)) + a + '%s,%sZ' % (fmt(cx - r), fmt(cy))


def shapes(svg_path):
    root = ET.parse(svg_path).getroot()
    vb = root.get('viewBox', '0 0 96 96').split()
    out = []
    for e in root.iter():
        tag = e.tag.replace(NS, '')
        if tag in ('svg',) or tag.startswith('{'):
            continue
        if tag == 'rect':
            d = rect_path(e)
        elif tag == 'circle':
            d = circle_path(e)
        elif tag == 'path':
            d = e.get('d')
        else:
            sys.exit('%s: elemento <%s> no admitido' % (svg_path, tag))
        out.append((d, e))
    return float(vb[2]), float(vb[3]), out


def android_xml(name):
    vw, vh, items = shapes(os.path.join(SRC, name + '.svg'))
    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             '<!-- GENERADO por tools/devicons/gen_devicons.py desde design/devices/%s.svg: no editar. -->' % name,
             '<vector xmlns:android="http://schemas.android.com/apk/res/android"',
             '    android:width="48dp" android:height="48dp"',
             '    android:viewportWidth="%s" android:viewportHeight="%s">' % (fmt(vw), fmt(vh))]
    for d, e in items:
        attrs = ['android:pathData="%s"' % d]
        fill = e.get('fill', '#000000')
        if fill != 'none':
            attrs.append('android:fillColor="%s"' % fill.upper())
        stroke = e.get('stroke')
        if stroke and stroke != 'none':
            attrs.append('android:strokeColor="%s"' % stroke.upper())
            attrs.append('android:strokeWidth="%s"' % fmt(e.get('stroke-width', '1')))
            cap = e.get('stroke-linecap')
            if cap:
                attrs.append('android:strokeLineCap="%s"' % cap)
            join = e.get('stroke-linejoin')
            if join:
                attrs.append('android:strokeLineJoin="%s"' % join)
        lines.append('    <path ' + '\n        '.join(attrs) + ' />')
    lines.append('</vector>')
    return '\n'.join(lines) + '\n'


def rgb565a8(png_path, size):
    from PIL import Image
    im = Image.open(png_path).convert('RGBA')
    if im.size != (size, size):
        sys.exit('%s: %s en vez de %dx%d' % (png_path, im.size, size, size))
    px = im.load()
    rgb = bytearray()
    alpha = bytearray()
    for y in range(size):
        for x in range(size):
            r, g, b, a = px[x, y]
            rgb += struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            alpha.append(a)
    return bytes(rgb + alpha)


def main():
    os.makedirs(ANDROID, exist_ok=True)
    for n in ANDROID_ICONS:
        with open(os.path.join(ANDROID, 'ic_dev_%s.xml' % n), 'w') as f:
            f.write(android_xml(n))
        print('android: ic_dev_%s.xml' % n)

    chunks = []
    with tempfile.TemporaryDirectory() as tmp:
        for n, size in WATCH_ICONS:
            png = os.path.join(tmp, n + '.png')
            subprocess.run(['node', os.path.join(os.path.dirname(__file__), 'rasterize.mjs'),
                            os.path.join(SRC, n + '.svg'), png, str(size)], check=True,
                           env=dict(os.environ, CHROMIUM=os.environ.get('CHROMIUM', '/opt/pw-browsers/chromium')))
            chunks.append((n, size, rgb565a8(png, size)))
            print('reloj: %s %dx%d' % (n, size, size))

    c = ['/* GENERADO por tools/devicons/gen_devicons.py desde design/devices/<modelo>.svg: no editar. */',
         '#include "flex_device_icons.h"', '']
    for n, size, data in chunks:
        c.append('/* %s: %dx%d RGB565A8 (%d B), alineado a 4 para el Xtensa */' % (n, size, size, len(data)))
        c.append('static const uint8_t k_%s[%d] __attribute__((aligned(4))) = {' % (n, len(data)))
        for i in range(0, len(data), 24):
            c.append('    ' + ', '.join('0x%02x' % b for b in data[i:i + 24]) + ',')
        c.append('};')
        c.append('const lv_image_dsc_t flex_devicon_%s = {' % n)
        c.append('    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .flags = 0,')
        c.append('               .w = %d, .h = %d, .stride = %d},' % (size, size, size * 2))
        c.append('    .data_size = sizeof(k_%s), .data = k_%s,' % (n, n))
        c.append('};')
        c.append('')
    with open(WATCH_C, 'w') as f:
        f.write('\n'.join(c))
    h = ['/* GENERADO por tools/devicons/gen_devicons.py: no editar. */',
         '/* Iconos de dispositivos Flex desde design/devices/<modelo>.svg (mismo dibujo que en Flex Phone). */',
         '#pragma once', '#include "lvgl.h"', '']
    for n, size, _ in chunks:
        h.append('extern const lv_image_dsc_t flex_devicon_%s; /* %dx%d */' % (n, size, size))
    with open(WATCH_H, 'w') as f:
        f.write('\n'.join(h) + '\n')
    print('reloj: %s' % os.path.relpath(WATCH_C, ROOT))


if __name__ == '__main__':
    main()
