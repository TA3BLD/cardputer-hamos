"""Cross-checks the HamOS SSTV code (src/lib/Sstv.h) against two independent
implementations: the Python package "sstv" (Rust core: encoder and decoder)
and "pysstv" (encoder).

  pip install sstv pysstv numpy pillow
  g++ -std=c++17 -O2 -I src test/host/sstv_tool.cpp -o /tmp/sstv_tool
  python tools/sstv/crosscheck.py /tmp/sstv_tool

1. Transmit: the Rust decoder must recover the picture HamOS sends.
2. Receive: HamOS must recover pictures sent by the Rust encoder, with noise
   and with a 0.1 % sample clock difference (slant).
Quality is the PSNR against the original picture (higher is better; above
about 20 dB the picture is clearly recognisable, above 28 dB it is good).
The "noise" case has about 10 dB signal to noise ratio in the audio band.
At 8 kHz the fast modes cannot be better than about 22-25 dB: the Rust
decoder reaches 18-22 dB on its own signals there.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
import sstv
from PIL import Image, ImageDraw

tool = sys.argv[1]
tmp = tempfile.mkdtemp()
rng = np.random.default_rng(3)
MODES = {"Martin M1": "MARTIN_1", "Martin M2": "MARTIN_2", "Scottie S1": "SCOTTIE_1", "Scottie S2": "SCOTTIE_2",
         "Scottie DX": "SCOTTIE_DX", "Robot 36": "ROBOT_36", "Robot 72": "ROBOT_72", "PD 90": "PD_90",
         "PD 120": "PD_120", "PD 180": "PD_180"}


def test_card(w, h):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
    for i, c in enumerate(bars):
        d.rectangle([i * w // 8, 0, (i + 1) * w // 8, h // 2], fill=c)
    for x in range(w):
        v = 255 * x // (w - 1)
        d.line([x, h // 2, x, 3 * h // 4], fill=(v, v, v))
    d.rectangle([w // 8, 3 * h // 4 + 8, 7 * w // 8, h - 8], fill=(40, 40, 120))
    d.text((w // 4, 3 * h // 4 + h // 16), "HamOS SSTV TEST", fill=(255, 255, 255))
    return img


def psnr(a, b):
    a = np.asarray(a.convert("RGB"), dtype=float)
    b = np.asarray(b.convert("RGB").resize((a.shape[1], a.shape[0])), dtype=float)
    mse = np.mean((a - b) ** 2)
    return 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)


def run(*args):
    return subprocess.run([tool] + list(args), capture_output=True, text=True, check=True).stdout.strip()


def resample(x, src, dst):
    t = np.arange(0, len(x) * dst / src) * src / dst
    return np.interp(t, np.arange(len(x)), x)


failures = 0
print("1. HamOS transmit -> sstv (Rust) receive, 11025 Hz")
for name, rust in MODES.items():
    mode = getattr(sstv.Mode, rust)
    card = test_card(mode.image_width, mode.image_height)
    ppm = os.path.join(tmp, "card.ppm")
    card.save(ppm)
    raw = os.path.join(tmp, "tx.raw")
    run("encode", name, ppm, raw, "11025")
    audio = np.fromfile(raw, dtype="<i2").astype(float)
    pics = sstv.decode(audio.astype(np.int16), 11025)
    ok = len(pics) == 1 and pics[0].info.get("sstv_mode") == mode
    q = psnr(card, pics[0]) if pics else 0
    rust_audio = np.asarray(sstv.encode(card, mode, 11025))
    rust_len = len(rust_audio) / 11025
    rust_q = psnr(card, sstv.decode(rust_audio, 11025)[0])        # the Rust code with its own signal
    print("   %-11s decoded=%s  PSNR %4.1f dB (sstv's own signal: %4.1f dB)  length %.2f s (sstv: %.2f s)" %
          (name, ok, q, rust_q, len(audio) / 11025, rust_len))
    failures += (not ok) or q < rust_q - 1.5 or abs(len(audio) / 11025 - rust_len) > 0.05

RX_RATE = int(os.environ.get("RX_RATE", "11025"))
print("2. sstv (Rust) transmit -> HamOS receive at %d Hz (noise; 0.1 %% clock error)" % RX_RATE)
for name, rust in MODES.items():
    mode = getattr(sstv.Mode, rust)
    card = test_card(mode.image_width, mode.image_height)
    audio = np.asarray(sstv.encode(card, mode, 48000), dtype=float)
    audio = audio / np.max(np.abs(audio)) * 12000
    results = []
    for label, rate_in, noise in (("clean", RX_RATE, 0), ("noise", RX_RATE, 2500), ("slant", RX_RATE * 1.001, 800)):
        x = resample(audio, 48000, rate_in)
        x = x + rng.normal(0, noise, len(x)) if noise else x
        x = np.concatenate([rng.normal(0, 300, 4000), x, rng.normal(0, 300, 4000)])
        raw = os.path.join(tmp, "rx.raw")
        np.clip(x, -32767, 32767).astype("<i2").tofile(raw)
        out = run("decode", raw, str(RX_RATE), os.path.join(tmp, "rx"))
        got, lines, slant, done = out.rsplit(" ", 3)
        q = psnr(card, Image.open(os.path.join(tmp, "rx-0.ppm"))) if os.path.exists(os.path.join(tmp, "rx-0.ppm")) else 0
        for f in os.listdir(tmp):
            if f.startswith("rx-"):
                os.remove(os.path.join(tmp, f))
        results.append("%s %4.1f dB" % (label, q))
        if got != name or int(lines) < mode.image_height or q < {"clean": 21, "noise": 13, "slant": 19}[label]:
            failures += 1
            results[-1] += " [FAIL: %s, %s lines, slant %s ppm]" % (got, lines, slant)
    print("   %-11s %s" % (name, " | ".join(results)))

print("FAILURES: %d" % failures)
sys.exit(1 if failures else 0)
