#!/bin/sh
# Compiles the firmware against the simulator stubs with (1) a display that only
# accepts uint16_t colours, (2) Arduino's common macros, (3) the old keyboard struct.
R=$(cd "$(dirname "$0")/../.." && pwd)
S=$(mktemp -d)
M=$R/tools/check/arduino_macros.h

rm -rf $S/strict $S/old && cp -r $R/tools/sim/stubs $S/strict && cp -r $R/tools/sim/stubs $S/old
export S
python3 - <<'PY'
import os
S = os.environ['S']
p=S+'/strict/M5Cardputer.h'; s=open(p).read()
s=s.replace('void fillScreen(int c) { fillRect(0, 0, 240, 135, c); }','void fillScreen(uint16_t c) { fillRect(0, 0, 240, 135, c); }\n    template <class T> void fillScreen(T) = delete;')
s=s.replace('void fillRect(int x, int y, int w, int h, int c) {','template <class T> void fillRect(int, int, int, int, T) = delete;\n    void fillRect(int x, int y, int w, int h, uint16_t c) {')
s=s.replace('void setTextColor(int f, int b) { fg = f; bg = b; }','void setTextColor(uint16_t f, uint16_t b) { fg = f; bg = b; }\n    template <class A, class B> void setTextColor(A, B) = delete;')
s=s.replace('void setTextColor(int f) { fg = f; bg = -1; }','void setTextColor(uint16_t f) { fg = f; bg = -1; }\n    template <class A> void setTextColor(A) = delete;')
for name in ['drawLine','drawPixel','drawCircle','fillCircle','drawRect','fillTriangle','drawFastHLine','drawFastVLine']:
    pass
for fn, args in [('drawLine','int,int,int,int'),('drawPixel','int,int'),('drawCircle','int,int,int'),('fillCircle','int,int,int'),('fillTriangle','int,int,int,int,int,int'),('drawRect','int,int,int,int')]:
    s=s.replace('    void setSwapBytes(bool) {}','    template <class T> void %s(%s, T) = delete;\n    void setSwapBytes(bool) {}' % (fn, args),1)
open(p,'w').write(s)
p=S+'/old/M5Cardputer.h'; s=open(p).read()
a=s.index('        bool backspace = false, del = false, enter = false, space = false, esc = false;'); b=s.index('    std::deque<KeysState> script;')
s=s[:a]+'''        bool del = false, enter = false, space = false;
        uint8_t count() const { return (uint8_t)(word.size() + tab + fn + shift + ctrl + opt + alt + del + enter); }
    };
'''+s[b:]
open(p,'w').write(s)
PY
cd $R/tools/sim
for dir in strict old; do
  g++ -std=gnu++17 -Wall -Wextra -Wno-deprecated-declarations -DHAMOS_SIM -I $S/$dir -I ../../src -include $M -fsyntax-only ../../src/main.cpp ../../src/core/*.cpp 2>&1 | grep -E "error|warning" -A3 | head -20
  echo "check with $dir stubs done"
done
# The real (not simulator) Bluetooth and I2C code against the library's signatures
cd $R
for f in Ble Weather; do
  g++ -std=gnu++17 -Wall -fsyntax-only -I tools/check/realcheck -I tools/sim/stubs -I src src/core/$f.cpp 2>&1 | grep -E "error" | head -10
done
echo "check of the BLE / I2C code done"
rm -rf $S
