#!/bin/sh
# Release files from a PlatformIO build, named like the GitHub releases:
#   hamOS_0x0.bin       complete image, written at address 0x0 (first install, web flasher, M5Burner)
#   hamOS_<ver>.bin     the app only, for updates (Update app, web interface, Launcher):
#                       version 1.1.0 -> hamOS_1_1.bin, 1.1.2 -> hamOS_1_1_2.bin
#   pio run -e cardputer-adv && sh tools/release.sh        -> dist/v<version>/
set -e
cd "$(dirname "$0")/.."
B=.pio/build/cardputer-adv
[ -f $B/firmware.bin ] || { echo "Build first: pio run -e cardputer-adv"; exit 1; }
PIO=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}
BOOT=$(find "$PIO/packages" -name boot_app0.bin | head -1)
ESPTOOL=$(find "$PIO/packages" -name esptool.py | head -1)
PY=$( [ -x "$PIO/penv/bin/python" ] && echo "$PIO/penv/bin/python" || echo python3 )
VERSION=$(sed -n 's/.*HAMOS_VERSION "\(.*\)".*/\1/p' src/core/Version.h)
NAME=$(echo "$VERSION" | sed 's/\.0$//; s/\./_/g')
OUT=${1:-dist/v$VERSION}
mkdir -p "$OUT"
"$PY" "$ESPTOOL" --chip esp32s3 merge_bin -o "$OUT/hamOS_0x0.bin" --flash_mode dio --flash_freq 80m --flash_size 8MB \
    0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0xe000 "$BOOT" 0x10000 $B/firmware.bin >/dev/null
cp $B/firmware.bin "$OUT/hamOS_$NAME.bin"
echo "$OUT:"
ls -l "$OUT"
