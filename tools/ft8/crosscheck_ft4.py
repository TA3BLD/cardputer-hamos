"""Cross-checks HamOS's FT4 (src/lib/Ft4.h) against ft8lib (a WSJT-X port).

  pip install ft8lib numpy
  g++ -std=c++17 -O2 -I src test/host/ft4_tool.cpp -o /tmp/ft4_tool
  python tools/ft8/crosscheck_ft4.py /tmp/ft4_tool

1. Tones identical for a set of messages.
2. ft8lib decodes the audio HamOS generates.
3. HamOS decodes ft8lib's audio with noise at several SNRs.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
import ft8lib

tool = sys.argv[1]
msgs = ["CQ TA3BLD KM38", "TA3BLD DL1ABC JO31", "DL1ABC TA3BLD -12", "TA3BLD DL1ABC R-07", "DL1ABC TA3BLD RR73",
        "CQ DX K1ABC FN42", "TA3BLD K1ABC 73", "CQ POTA TA3BLD KM38", "PY2AA TA3BLD R+05", "CQ JA1XYZ PM95"]

# 1. tones
same = 0
for m in msgs:
    ref = "".join(str(int(t)) for t in ft8lib.ft4_tones_from_bits(ft8lib.pack77(m)))
    ours = subprocess.run([tool, "tones", m], capture_output=True, text=True).stdout.strip()
    same += ref == ours
    if ref != ours:
        print("DIFF", m, "\n ref ", ref, "\n ours", ours)
print(f"tones identical: {same}/{len(msgs)}")
assert same == len(msgs)

# 2. our audio -> ft8lib
tmp = tempfile.mkdtemp()
ok = 0
for i, m in enumerate(msgs):
    path = os.path.join(tmp, "tx.raw")
    subprocess.run([tool, "synth", m, str(800 + 150 * i), path], check=True)
    a8 = np.fromfile(path, dtype=np.int16).astype(np.float64) / 32768
    # 8000 -> 12000 Hz by FFT resampling
    spec = np.fft.rfft(a8)
    n12 = len(a8) * 3 // 2
    spec12 = np.zeros(n12 // 2 + 1, dtype=complex)
    spec12[:len(spec)] = spec
    a12 = np.fft.irfft(spec12, n12) * 1.5
    a12 = np.concatenate([a12, np.zeros(int(7.5 * 12000) - len(a12))]) if len(a12) < 90000 else a12[:90000]
    a12 += np.random.normal(0, 0.003, len(a12))
    dec = ft8lib.decode_ft4(a12)
    texts = [d.message.strip() for d in dec]
    ok += m in texts
    if m not in texts:
        print("ft8lib did not decode", m, texts)
print(f"ft8lib decodes HamOS audio: {ok}/{len(msgs)}")
assert ok == len(msgs)

# 3. ft8lib audio -> ours, with noise
rng = np.random.default_rng(3)
for snr in [0, -6, -10, -13, -15, -17]:
    good = 0
    trials = 12
    for k in range(trials):
        m = msgs[k % len(msgs)]
        f0 = 600 + 170 * (k % 12)
        wave = ft8lib.encode_ft4(m, f0=f0)
        start = int((0.5 + rng.uniform(-0.2, 0.3)) * 12000)
        a12 = np.zeros(90000)
        a12[start:start + len(wave)] = wave[: 90000 - start]
        sig = np.sqrt(np.mean(wave ** 2))
        # noise in 2500 Hz for the wanted SNR, at 12 kHz sample rate (6000 Hz bandwidth)
        noise_rms = sig / (10 ** (snr / 20)) * np.sqrt(6000 / 2500)
        a12 += rng.normal(0, noise_rms, len(a12))
        # 12000 -> 8000 Hz
        spec = np.fft.rfft(a12)
        n8 = len(a12) * 2 // 3
        a8 = np.fft.irfft(spec[: n8 // 2 + 1], n8) * (2 / 3)
        a8 = a8 / max(1e-9, np.max(np.abs(a8))) * 20000
        path = os.path.join(tmp, "rx.raw")
        a8.astype(np.int16).tofile(path)
        out = subprocess.run([tool, "decode", path], capture_output=True, text=True).stdout
        good += any(line.split(" ", 3)[3] == m for line in out.strip().splitlines() if line.count(" ") >= 3)
    print(f"SNR {snr:+3d} dB: {good}/{trials} decoded")
