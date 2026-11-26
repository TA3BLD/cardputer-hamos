"""Makes the sample pictures in sdcard/HamOS/sstv (320 x 256, 24-bit BMP).

  python tools/sstv/make_samples.py sdcard/HamOS/sstv
"""
import os
import struct
import sys
from PIL import Image, ImageDraw

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
W, H = 320, 256

# 1. Test card
img = Image.new("RGB", (W, H), (0, 0, 0))
d = ImageDraw.Draw(img)
bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
for i, c in enumerate(bars):
    d.rectangle([i * W // 8, 40, (i + 1) * W // 8 - 1, 140], fill=c)
for x in range(W):
    v = 255 * x // (W - 1)
    d.line([x, 140, x, 175], fill=(v, v, v))
d.ellipse([110, 60, 210, 160], outline=(255, 255, 255), width=3)
d.rectangle([0, 180, W - 1, H - 1], fill=(20, 30, 90))
d.text((12, 196), "HamOS SSTV test card", fill=(255, 255, 255))
d.text((12, 216), "Martin, Scottie, Robot, PD", fill=(200, 220, 255))
img.save(os.path.join(out, "testcard.bmp"))

# 2. A simple QSL picture: sky, sea, mast and waves
img = Image.new("RGB", (W, H))
d = ImageDraw.Draw(img)
for y in range(H):
    if y < 170:
        t = y / 170
        d.line([0, y, W, y], fill=(int(30 + 60 * t), int(80 + 100 * t), int(160 + 80 * t)))
    else:
        t = (y - 170) / 86
        d.line([0, y, W, y], fill=(10, int(60 - 30 * t), int(120 - 50 * t)))
d.ellipse([240, 30, 290, 80], fill=(255, 220, 120))
d.polygon([(40, 170), (130, 90), (220, 170)], fill=(60, 70, 60))
d.line([160, 170, 160, 40], fill=(220, 220, 220), width=3)
for k in range(4):
    r = 15 + 14 * k
    d.arc([160 - r, 40 - r, 160 + r, 40 + r], 200, 340, fill=(255, 255, 255), width=2)
d.rectangle([0, 214, W - 1, H - 1], fill=(0, 0, 0))
d.text((12, 226), "TNX QSO  73", fill=(255, 255, 255))
img.save(os.path.join(out, "qsl.bmp"))
for f in os.listdir(out):
    print(f, os.path.getsize(os.path.join(out, f)))
