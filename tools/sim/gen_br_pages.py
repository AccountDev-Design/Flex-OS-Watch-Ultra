#!/usr/bin/env python3
"""
Páginas de prueba del navegador para la vista previa (F6): una página
inventada de 410 px de ancho, rasterizada como haría el servicio remoto y
cortada en vistas de 410x376 (la zona de página del reloj) a distintos desplazamientos, en JPEG BASELINE
(lo único que acepta FBP/1). No es una web real: solo sirve para ver el
lienzo, el desplazamiento y el teclado en la vista previa.
Uso: python3 gen_br_pages.py <carpeta de salida>
"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H, VH = 410, 1500, 376
OFFS = [0, 150, 300, 450, 600, 750, 900, 1124]
F = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FB = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

def main(out):
    im = Image.new('RGB', (W, H), (250, 250, 247))
    d = ImageDraw.Draw(im)
    h1, h2, p, s = ImageFont.truetype(FB, 26), ImageFont.truetype(FB, 19), ImageFont.truetype(F, 16), ImageFont.truetype(F, 13)
    d.rectangle([0, 0, W, 56], fill=(36, 92, 196))
    d.text((16, 15), 'Ejemplo · Noticias del taller', font=h2, fill=(255, 255, 255))
    y = 76
    d.text((16, y), 'Una página de prueba', font=h1, fill=(20, 24, 32)); y += 40
    d.text((16, y), 'Servida por el servicio de navegación', font=s, fill=(110, 116, 128)); y += 30
    para = ('El reloj no ejecuta la web: el servicio la abre en un navegador de verdad, '
            'la convierte en imágenes JPEG y el reloj solo pinta, desplaza y toca. '
            'Esta página existe solo para la vista previa del simulador.')
    words, line = para.split(), ''
    for w in words:
        if d.textlength(line + ' ' + w, font=p) > W - 32:
            d.text((16, y), line.strip(), font=p, fill=(40, 44, 52)); y += 24; line = ''
        line += ' ' + w
    d.text((16, y), line.strip(), font=p, fill=(40, 44, 52)); y += 40
    d.rounded_rectangle([16, y, W - 16, y + 52], radius=10, outline=(150, 156, 170), width=2, fill=(255, 255, 255))
    d.text((30, y + 15), 'Buscar en el sitio...', font=p, fill=(150, 156, 170)); y += 80
    colors = [(232, 98, 72), (60, 160, 110), (240, 180, 60), (120, 100, 210), (60, 150, 220)]
    for i in range(9):
        c = colors[i % len(colors)]
        d.rounded_rectangle([16, y, W - 16, y + 110], radius=14, fill=(255, 255, 255), outline=(225, 228, 232))
        d.rounded_rectangle([28, y + 14, 110, y + 96], radius=10, fill=c)
        d.text((126, y + 18), f'Artículo {i + 1}', font=h2, fill=(20, 24, 32))
        d.text((126, y + 50), 'Texto de ejemplo para', font=s, fill=(90, 96, 108))
        d.text((126, y + 70), 'comprobar el desplazamiento', font=s, fill=(90, 96, 108))
        y += 126
    d.text((16, H - 40), 'Fin de la página de prueba', font=s, fill=(110, 116, 128))
    for k, o in enumerate(OFFS):
        im.crop((0, o, W, o + VH)).save(f'{out}/br_page_{k}.jpg', 'JPEG', quality=70, progressive=False,
                                         optimize=False, subsampling=2)
    # una imagen PROGRESIVA: FBP/1 la prohíbe y el reloj debe rechazarla
    im.crop((0, 0, 64, 64)).save(f'{out}/br_prog.jpg', 'JPEG', quality=70, progressive=True)
    print(len(OFFS), 'vistas')

if __name__ == '__main__':
    main(sys.argv[1])
