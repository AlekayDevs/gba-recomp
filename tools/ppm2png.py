"""Converts the PPM screenshots of the headless runner to PNG (optionally scaled): python tools/ppm2png.py in.ppm out.png [scale]"""
import struct, sys, zlib

src, dst = sys.argv[1], sys.argv[2]
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
d = open(src, 'rb').read()
w, h = 240, 160
pix = d[len(d) - w * h * 3:]
rows = []
for y in range(h):
    row = pix[y * w * 3:(y + 1) * w * 3]
    if scale > 1:
        row = b''.join(row[i:i + 3] * scale for i in range(0, len(row), 3))
    rows += [b'\x00' + row] * scale
raw = b''.join(rows)


def chunk(t, b):
    c = struct.pack('>I', len(b)) + t + b
    return c + struct.pack('>I', zlib.crc32(t + b))


png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
open(dst, 'wb').write(png)
