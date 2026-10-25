import sys
from fontTools.ttLib import TTFont
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
src, dst, sx = sys.argv[1], sys.argv[2], float(sys.argv[3])
f = TTFont(src)
gs = f.getGlyphSet()
glyf = f['glyf']; hmtx = f['hmtx']
new = {}
for name in f.getGlyphOrder():
    pen = TTGlyphPen(gs)
    gs[name].draw(TransformPen(pen, (sx, 0, 0, 1, 0, 0)))
    new[name] = pen.glyph()
for name, g in new.items():
    glyf[name] = g
    w, lsb = hmtx[name]
    hmtx[name] = (int(round(w * sx)), int(round(lsb * sx)))
for rec in f['name'].names:
    if rec.nameID in (1, 3, 4, 6, 16):
        rec.string = {1: 'Flex Condensed', 3: 'FlexCondensed', 4: 'Flex Condensed', 6: 'FlexCondensed', 16: 'Flex Condensed'}[rec.nameID]
if 'gvar' in f: del f['gvar']
f.save(dst)
