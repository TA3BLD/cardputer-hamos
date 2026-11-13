"""Turns the drawing commands recorded by sim.out into PNG screenshots.

Usage: render.py ops.txt OUTPUT_DIR
The font is a generic 5x7 LCD font (table from the MIT-licensed luma.core
project), close to but not identical to the one on the device.
"""
import os
import sys
from PIL import Image, ImageDraw

W, H, SCALE = 240, 135, 3
# 5 bytes per character for ASCII 32..126, one byte per column, bit 0 = top row.
FONT = bytes.fromhex(
    "000000000000005f00000003000300147f147f14242a7f2a12231308646236495522500005030000001c2241000041221c0014083e081408083e080800503000000808080808006060000020100804023e5149453e00427f400042615149462141454b311814127f1027454545393c4a49493001710905033649494936064949291e003636000000563600000814224100141414141400412214080201510906324979413e7e1111117e7f494949363e414141227f4141221c7f494949417f090909013e4149497a7f0808087f00417f41002040413f017f081422417f404040407f020c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946464949493101017f01013f4040403f1f2040201f3f4038403f631408146307087008076151494543007f41410002040810200041417f0004020102044040404040000102040020545454787f484444383844444420384444487f3854545418087e0901020c5252523e7f0804047800447d40002040443d007f1028440000417f40007c041804787c0804047838444444387c14141408081414187c7c080404084854545420043f4440203c4040207c1c2040201c3c4030403c44281028440c5050503c4464544c44000836410000007f000000413608001008081008")


def rgb(c565):
    r, g, b = (c565 >> 11) & 31, (c565 >> 5) & 63, c565 & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def draw_text(img, x, y, size, datum, fg, bg, text):
    if not text:
        return
    cw, ch = 6 * size, 8 * size
    width = len(text) * cw
    if datum in (1, 4):          # centred horizontally
        x -= width / 2
    elif datum == 3:             # right aligned
        x -= width
    if datum in (2, 3, 4):       # centred vertically
        y -= ch / 2
    draw = ImageDraw.Draw(img)
    draw.rectangle([int(x), int(y), int(x + width) - 1, int(y + ch) - 1], fill=rgb(bg))
    for i, char in enumerate(text):
        code = ord(char) if 32 <= ord(char) <= 126 else ord("?")
        cols = FONT[(code - 32) * 5:(code - 32) * 5 + 5]
        for cx in range(5):
            for cy in range(7):
                if cols[cx] >> cy & 1:
                    x0, y0 = x + (i * 6 + cx) * size, y + cy * size
                    draw.rectangle([int(x0), int(y0), int(x0 + size + 0.5) - 1, int(y0 + size + 0.5) - 1], fill=rgb(fg))


def main(ops_path, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    img = Image.new("RGB", (W, H))
    shots = []
    for line in open(ops_path):
        line = line.rstrip("\n")
        kind, _, rest = line.partition(" ")
        if kind == "R":
            x, y, w, h, c = map(int, rest.split())
            if w > 0 and h > 0:
                ImageDraw.Draw(img).rectangle([x, y, x + w - 1, y + h - 1], fill=rgb(c))
        elif kind == "T":
            x, y, size, datum, fg, bg, text = (rest.split(" ", 6) + [""])[:7]
            draw_text(img, int(x), int(y), float(size), int(datum), int(fg), int(bg), text)
        elif kind == "L":
            x0, y0, x1, y1, c = map(int, rest.split())
            ImageDraw.Draw(img).line([x0, y0, x1, y1], fill=rgb(c))
        elif kind == "C":
            x, y, r, c, filled = map(int, rest.split())
            box = [x - r, y - r, x + r, y + r]
            if filled:
                ImageDraw.Draw(img).ellipse(box, fill=rgb(c))
            else:
                ImageDraw.Draw(img).ellipse(box, outline=rgb(c))
        elif kind == "G":
            x0, y0, x1, y1, x2, y2, c = map(int, rest.split())
            ImageDraw.Draw(img).polygon([(x0, y0), (x1, y1), (x2, y2)], fill=rgb(c))
        elif kind == "F":
            x, y, w, h, c = map(int, rest.split())
            if w > 0 and h > 0:
                ImageDraw.Draw(img).rectangle([x, y, x + w - 1, y + h - 1], outline=rgb(c))
        elif kind == "I":
            x, y, w, h, data = rest.split()
            x, y, w, h = int(x), int(y), int(w), int(h)
            raw = bytes.fromhex(data)
            px = img.load()
            for j in range(h):
                for i in range(w):
                    if 0 <= x + i < W and 0 <= y + j < H:
                        k = 2 * (j * w + i)
                        px[x + i, y + j] = rgb(raw[k] << 8 | raw[k + 1])
        elif kind == "S":
            big = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
            big.save(os.path.join(out_dir, "%02d-%s.png" % (len(shots) + 1, rest)))
            shots.append(big)

    # Overview sheet: all screens in a grid.
    cols, pad = 4, 14
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (W * SCALE + pad) + pad, rows * (H * SCALE + pad) + pad), (24, 26, 32))
    for i, shot in enumerate(shots):
        x = pad + (i % cols) * (W * SCALE + pad)
        y = pad + (i // cols) * (H * SCALE + pad)
        ImageDraw.Draw(sheet).rectangle([x - 3, y - 3, x + W * SCALE + 2, y + H * SCALE + 2], fill=(70, 74, 84))
        sheet.paste(shot, (x, y))
    sheet.save(os.path.join(out_dir, "overview.png"))
    print("%d screenshots written to %s" % (len(shots), out_dir))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
