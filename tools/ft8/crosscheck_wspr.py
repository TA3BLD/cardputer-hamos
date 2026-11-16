"""Checks HamOS's WSPR tones (src/lib/Wspr.h) against ft8lib.
  g++ -std=c++17 -I src test/host/wspr_tool.cpp -o /tmp/wspr_tool
  python tools/ft8/crosscheck_wspr.py /tmp/wspr_tool
"""
import subprocess
import sys

import ft8lib

tool = sys.argv[1]
cases = [("TA3BLD", "KM38", 23), ("K1ABC", "FN42", 37), ("G4XYZ", "IO91", 10), ("DL1ABC", "JO31", 30), ("W1AW", "FN31", 0),
         ("JA1XYZ", "PM95", 60), ("VK2DX", "QF56", 33), ("9A1A", "JN75", 20), ("ZS6ABC", "KG33", 7), ("PY2AA", "GG66", 13)]
same = 0
for call, grid, dbm in cases:
    ref = "".join(str(int(t)) for t in ft8lib.wspr_tones_from_message(f"{call} {grid} {dbm}"))
    ours = subprocess.run([tool, call, grid, str(dbm)], capture_output=True, text=True).stdout.strip()
    same += ref == ours
    if ref != ours:
        print("DIFF", call, grid, dbm, "\n ref ", ref, "\n ours", ours)
print(f"WSPR tones identical: {same}/{len(cases)}")
assert same == len(cases)

# ft8lib decodes the audio HamOS sends
import os
import tempfile

import numpy as np

path = os.path.join(tempfile.mkdtemp(), "w.raw")
subprocess.run([tool, "TA3BLD", "KM38", "23", path], check=True, capture_output=True)
a8 = np.fromfile(path, dtype=np.int16).astype(np.float64) / 32768
spec = np.fft.rfft(a8)
n12 = len(a8) * 3 // 2
spec12 = np.zeros(n12 // 2 + 1, dtype=complex)
spec12[: len(spec)] = spec
a12 = np.fft.irfft(spec12, n12) * 1.5 + np.random.default_rng(1).normal(0, 0.05, n12)
dec = ft8lib.decode_wspr(a12)
texts = [" ".join(d.message.split()) for d in dec]
print("ft8lib decodes HamOS WSPR audio:", texts)
assert "TA3BLD KM38 23" in texts
