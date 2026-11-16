"""Dibuja el icono genérico de gba-port (arte propio) y escribe assets/gba-port.ico: una consola portátil sobre fondo azul.

    python tools/make_icon.py
Necesita Pillow. El .ico se sube al repositorio. Cada juego pone el suyo con GBA_PORT_ICON al compilar el paquete.
"""
import pathlib
from PIL import Image, ImageDraw

N = 1024
img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
g = Image.new("RGBA", (N, N))
gd = ImageDraw.Draw(g)
for y in range(N):
    t = y / N
    gd.line([(0, y), (N, y)], fill=(int(24 + 8 * t), int(52 - 12 * t), int(86 - 26 * t), 255))
m = Image.new("L", (N, N), 0)
ImageDraw.Draw(m).rounded_rectangle([0, 0, N - 1, N - 1], radius=int(N * .22), fill=255)
img.paste(g, (0, 0), m)
d = ImageDraw.Draw(img)
gold = (232, 197, 71, 255)
d.rounded_rectangle([N * .03, N * .03, N * .97, N * .97], radius=int(N * .19), outline=gold, width=int(N * .028))
s = N / 256
body = (146, 150, 210, 255)
d.rounded_rectangle([34 * s, 82 * s, 222 * s, 178 * s], radius=int(26 * s), fill=body, outline=(60, 62, 110, 255), width=int(4 * s))
d.rounded_rectangle([82 * s, 92 * s, 174 * s, 168 * s], radius=int(8 * s), fill=(34, 40, 44, 255))
d.rounded_rectangle([90 * s, 100 * s, 166 * s, 160 * s], radius=int(4 * s), fill=(140, 200, 120, 255))
d.rectangle([50 * s, 124 * s, 70 * s, 132 * s], fill=(40, 40, 70, 255)); d.rectangle([56 * s, 118 * s, 64 * s, 138 * s], fill=(40, 40, 70, 255))
d.ellipse([186 * s, 114 * s, 202 * s, 130 * s], fill=(200, 60, 80, 255)); d.ellipse([200 * s, 132 * s, 216 * s, 148 * s], fill=(200, 60, 80, 255))
out = pathlib.Path(__file__).resolve().parent.parent / "assets" / "gba-port.ico"
img.save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("escrito", out)
