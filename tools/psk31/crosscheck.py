"""Decodes PSK31 made by an independent generator (written here from the
specification, with fldigi's Varicode table) with the HamOS receiver.

  g++ -std=c++17 -O2 -I src test/host/psk31_tool.cpp -o /tmp/psk31_tool
  python tools/psk31/crosscheck.py /tmp/psk31_tool pskvaricode.cxx

BPSK31: 31.25 baud; a 0 bit is a phase reversal, a 1 bit none; reversals
are shaped with a cosine so the amplitude passes through zero.
"""
import re
import subprocess
import sys
import tempfile

import numpy as np

tool, ref = sys.argv[1], sys.argv[2]
src = open(ref, encoding="latin-1").read()
table = re.findall(r'"([01]+)"', src[src.index("varicodetab1"):src.index("varicodetab2")])[:128]


def generate(text, hz, fs, snr_db, seed):
    bits = "0" * 40 + "".join(table[ord(c)] + "00" for c in text) + "1" * 30
    sps = fs / 31.25
    n = int(len(bits) * sps)
    t = np.arange(n) / fs
    sym = (np.arange(n) / sps).astype(int)
    frac = np.arange(n) / sps - sym
    sign = np.ones(len(bits) + 1)
    for k, b in enumerate(bits):
        sign[k + 1] = -sign[k] if b == "0" else sign[k]
    env = np.where(np.array([bits[k] for k in sym]) == "0", sign[sym] * np.cos(np.pi * frac), sign[sym])
    x = 8000 * env * np.cos(2 * np.pi * hz * t + 0.7)
    x = np.concatenate([np.zeros(fs), x, np.zeros(fs)])
    sigma = np.sqrt(8000 ** 2 / 2 / 10 ** (snr_db / 10) * (fs / 2) / 2500)
    return x + np.random.default_rng(seed).normal(0, sigma, len(x))


failures = 0
text = "CQ CQ de TA3BLD: Merhaba from an independent PSK31 generator, 599 73!"
for fs, hz, tune, snr in ((8000, 1000, 1000, 20), (8000, 1734.4, 1720, 5), (8000, 600, 610, 0), (11025, 1500, 1500, 10)):
    raw = tempfile.mktemp(suffix=".raw")
    np.clip(generate(text, hz, fs, snr, 1), -32767, 32767).astype("<i2").tofile(raw)
    got = subprocess.run([tool, "decode", raw, str(fs), str(tune)], capture_output=True, text=True).stdout.strip()
    ok = text in got
    print("%5d Hz sampling, signal %.1f Hz, tuned %d Hz, SNR %+d dB: %s" % (fs, hz, tune, snr, "ok" if ok else "FAIL: " + repr(got)))
    failures += not ok
sys.exit(1 if failures else 0)
