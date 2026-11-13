"""Converts country flag pictures into the small files HamOS draws.

Source: the public-domain flag set of the npm package "svg-country-flags"
(png250px folder):  npm pack svg-country-flags && tar xzf svg-country-flags-*.tgz
  python tools/flags/make_flags.py package/png250px sdcard/HamOS/flags

Output per flag, in two sizes (s = 16x11 for lists, l = 48x32 for pages):
  "FL", width (1 byte), height (1 byte), then width*height RGB565 pixels,
  big-endian (high byte first), rows top to bottom.
The flag keeps its proportions inside the box; the rest is black.
"""
import os
import sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
sizes = {"s": (16, 11), "l": (48, 32)}
for sub in sizes:
    os.makedirs(os.path.join(out, sub), exist_ok=True)

count = 0
for name in sorted(os.listdir(src)):
    if not name.endswith(".png"):
        continue
    code = name[:-4]
    img = Image.open(os.path.join(src, name)).convert("RGBA")
    for sub, (w, h) in sizes.items():
        scale = min(w / img.width, h / img.height)
        fw, fh = max(1, round(img.width * scale)), max(1, round(img.height * scale))
        small = img.resize((fw, fh), Image.LANCZOS)
        box = Image.new("RGBA", (w, h), (0, 0, 0, 255))
        box.alpha_composite(small, ((w - fw) // 2, (h - fh) // 2))
        data = bytearray(b"FL" + bytes([w, h]))
        for r, g, b, a in box.getdata():
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            data += bytes([v >> 8, v & 0xFF])
        open(os.path.join(out, sub, code + ".bin"), "wb").write(data)
    count += 1
print("%d flags written" % count)
