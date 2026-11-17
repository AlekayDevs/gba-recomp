"""Dibuja el icono de la app Android (arte propio, pixel art: la gorra de Ezlo sobre una placa verde con marco de oro).

    python tools/make_android_icon.py
Necesita Pillow. Escribe android/app/src/main/res/drawable-nodpi/ic_bg.png y ic_fg.png (icono adaptable, 432x432) y
mipmap-xxxhdpi/ic_launcher.png (192x192, por si acaso). Los colores son los de runtime/play_touch.h.
"""
import pathlib
from PIL import Image

OUT, GOLD_HI, GOLD, GOLD_LO, GOLD_DK = 0x14240C, 0xFFF0A0, 0xF0C840, 0xB07818, 0x684010
HAT = ["......gg.....", ".....gGgg....", "....gGgggg...", "...gGggggg...", "..gGgggggg...", ".gGggggggg...", ".gGgggggggg..",
       "gGgggggggggg.", "bBBBBBBBBBBb.", "bbbbbbbbbbbb.", ".bbbbbbbbbb.."]
G = 54  # cuadrícula: 54 celdas de 8 px = 432 px

def rgb(c): return ((c >> 16) & 255, (c >> 8) & 255, c & 255, 255)

def background():
    im = Image.new("RGBA", (G, G))
    bands = [0x0A1E18, 0x0D261E, 0x102E24, 0x14382A, 0x18422F]
    for y in range(G):
        b = min(y * len(bands) // G, len(bands) - 1)
        for x in range(G):
            c = bands[b]
            if b + 1 < len(bands) and (y * len(bands)) % G < len(bands) and (x + y) % 2 == 0: c = bands[b + 1]
            im.putpixel((x, y), rgb(c))
    s = 12345
    for i in range(22):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF; x = s % G
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF; y = s % G
        im.putpixel((x, y), rgb(GOLD_HI) if i % 3 == 0 else (0x80, 0x78, 0x30, 255))
    return im

def plate(im, l, t, r, b):
    def cut(i, col):
        for y in range(t + i, b - i):
            for x in range(l + i, r - i):
                if (x in (l + i, r - i - 1)) and (y in (t + i, b - i - 1)): continue
                im.putpixel((x, y), rgb(col))
    cut(0, OUT); cut(1, GOLD_HI)
    for y in range(t + 1, b - 1):
        for x in range(l + 1, r - 1):
            if x >= r - 3 or y >= b - 3: im.putpixel((x, y), rgb(GOLD_LO))
    cut(3, GOLD_DK); cut(4, 0x1E4A30)
    for x in range(l + 4, r - 4):
        im.putpixel((x, t + 4), rgb(0x2F6E46))

def hat(im, ox, oy, k):
    def put(x, y, c):
        for j in range(k):
            for i in range(k): im.putpixel((ox + x * k + i, oy + y * k + j), rgb(c))
    for y in range(-1, 12):
        for x in range(-1, 14):
            inside = 0 <= y < 11 and 0 <= x < 13 and HAT[y][x] != "."
            near = any(0 <= y + dy < 11 and 0 <= x + dx < 13 and HAT[y + dy][x + dx] != "." for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if not inside and near: put(x, y, OUT)
    for y in range(11):
        for x in range(13):
            c = HAT[y][x]
            if c != ".": put(x, y, {"g": 0x3CC060, "G": 0xA0F0A0, "b": GOLD_LO, "B": GOLD_HI}[c])

def foreground():
    im = Image.new("RGBA", (G, G), (0, 0, 0, 0))
    plate(im, 9, 9, 45, 45)
    hat(im, 27 - 13, 27 - 11 - 1, 2)
    return im

res = pathlib.Path(__file__).resolve().parent.parent / "android/app/src/main/res"
(res / "drawable-nodpi").mkdir(parents=True, exist_ok=True)
(res / "mipmap-xxxhdpi").mkdir(parents=True, exist_ok=True)
bg, fg = background(), foreground()
bg.resize((432, 432), Image.NEAREST).save(res / "drawable-nodpi/ic_bg.png")
fg.resize((432, 432), Image.NEAREST).save(res / "drawable-nodpi/ic_fg.png")
full = bg.copy(); full.alpha_composite(fg)
full.resize((192, 192), Image.NEAREST).save(res / "mipmap-xxxhdpi/ic_launcher.png")
(res / "mipmap-anydpi-v26").mkdir(exist_ok=True)
(res / "mipmap-anydpi-v26/ic_launcher.xml").write_text(
    '<?xml version="1.0" encoding="utf-8"?>\n<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
    '    <background android:drawable="@drawable/ic_bg" />\n    <foreground android:drawable="@drawable/ic_fg" />\n</adaptive-icon>\n')
print("ok")
