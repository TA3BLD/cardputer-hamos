#!/bin/sh
# Makes the single-file firmware M5Burner wants (written at address 0) from a
# PlatformIO build, plus the description and cover picture, in dist/m5burner/.
#   pio run -e cardputer-adv && sh tools/m5burner/make_package.sh
set -e
cd "$(dirname "$0")/../.."
B=.pio/build/cardputer-adv
[ -f $B/firmware.bin ] || { echo "Build first: pio run -e cardputer-adv"; exit 1; }
PIO=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}
BOOT=$(find "$PIO/packages" -name boot_app0.bin | head -1)
ESPTOOL=$(find "$PIO/packages" -name esptool.py | head -1)
VERSION=$(sed -n 's/.*HAMOS_VERSION "\(.*\)".*/\1/p' src/core/Version.h)
OUT=dist/m5burner
mkdir -p $OUT
python3 "$ESPTOOL" --chip esp32s3 merge_bin -o $OUT/hamOS_0x0.bin --flash_mode dio --flash_freq 80m --flash_size 8MB \
    0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0xe000 "$BOOT" 0x10000 $B/firmware.bin
cp tools/m5burner/description.txt $OUT/
cp docs/screens/05-launcher.png $OUT/cover.png
echo "Ready in $OUT:"
ls -l $OUT
