#!/bin/sh
# Builds the simulator, runs the key script in sim.cpp and writes screenshots
# to docs/screens/. Needs g++ and Python with Pillow.
set -e
cd "$(dirname "$0")"
g++ -std=gnu++17 -Wall -Wextra -I stubs -I ../../src sim.cpp ../../src/core/*.cpp -o sim.out
rm -rf simsd && mkdir -p simsd/HamOS
printf 'callsign=TA3BLD\ngrid=KM38nk\nwifi_ssid=HomeWiFi\nwifi_pass=secret\n' > simsd/HamOS/config.txt
./sim.out > ops.txt
"${PYTHON:-python3}" render.py ops.txt ../../docs/screens
