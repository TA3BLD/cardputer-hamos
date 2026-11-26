"""Compares the Varicode table in src/lib/Psk31.h with fldigi's.

  curl -O https://raw.githubusercontent.com/w1hkj/fldigi/master/src/psk/pskvaricode.cxx
  python tools/psk31/check_varicode.py pskvaricode.cxx
"""
import re
import sys

ref_src = open(sys.argv[1], encoding="latin-1").read()
ref = re.findall(r'"([01]+)"', ref_src[ref_src.index("varicodetab1"):ref_src.index("varicodetab2")])[:128]
mine_src = open("src/lib/Psk31.h").read()
mine = re.findall(r'"([01]+)"', mine_src[mine_src.index("table[128]"):mine_src.index("return c >= 0")])
assert len(ref) == 128 and len(mine) == 128, (len(ref), len(mine))
bad = [(i, m, r) for i, (m, r) in enumerate(zip(mine, ref)) if m != r]
for i, m, r in bad:
    print("code %3d %r: mine %s, fldigi %s" % (i, chr(i) if 32 <= i < 127 else "", m, r))
# Varicode rules: no "00" inside a code, starts and ends with 1, all different.
for i, m in enumerate(mine):
    assert "00" not in m and m[0] == "1" and m[-1] == "1", i
assert len(set(mine)) == 128
print("%d of 128 codes identical to fldigi" % (128 - len(bad)))
sys.exit(1 if bad else 0)
