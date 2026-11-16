# HamOS - notes for Claude

Amateur radio firmware for the M5Stack Cardputer ADV (ESP32-S3, 240x135 screen,
keyboard) with the LoRa (SX1262) + GNSS cap. Arduino framework via PlatformIO.
The owner speaks Turkish: answer in Turkish; code, comments and commits in English.

## State
- Everything was written and tested on a PC (simulator, unit tests, cross-checks
  against reference libraries) but has **not yet been built with PlatformIO or run
  on the device**. The first real builds will show compile errors: fix them in the
  smallest way, keep behaviour, rerun the checks below.
- GitHub Actions does not start on this account (billing / settings), so there is
  no CI build yet. The repository stays private.
- Work branch: `claude/amateur-radio-os-project-klmhfh`.

## Build and flash
    pio run -e cardputer-adv                 # build
    pio run -e cardputer-adv -t upload       # flash (if no port: hold G0 while plugging in)
    pio device monitor                       # USB serial (also used by the PC panel)
Copy `sdcard/` to the root of a FAT32 microSD (maps, flags, guide pages).

## Checks (run after every change)
    sh tools/sim/run.sh                      # simulator: end-to-end script + screenshots in docs/screens
    sh tools/check/strict_check.sh           # Arduino macros, uint16_t colours, old keyboard struct, real BLE/I2C signatures
    for t in test_libs test_maidenhead test_radio test_modem test_sat test_ft8qso test_psk31 test_json test_web test_cat test_wx; do
      g++ -std=c++17 -O1 -I src test/host/$t.cpp -lcrypto -o test/host/t.out && test/host/t.out; done
Cross-checks with Python reference packages (pip install ft8lib sstv pysstv aprslib sgp4 meshtastic numpy):
`tools/ft8/crosscheck*.py`, `tools/sstv/crosscheck.py`, `tools/psk31/crosscheck.py`,
`tools/pskreporter/check_ipfix.py`, `tools/sim/mesh_app_check.py` (with `tools/sim/devsim.cpp`).
`tools/sim/devsim.cpp` runs the firmware live on the PC (USB port on a pty, web server, Meshtastic API).

## Layout
- `src/main.cpp`: app registry (one table of app + launcher group), main loop.
- `src/apps/*.h`: one class per screen, derived from `App` (`src/core/App.h`).
- `src/core/`: services with hardware (mesh, LoRa, GPS, audio, rig/CAT, BLE, USB link, web, updater ...).
- `src/lib/`: pure C++ (protocols, codecs, DSP), tested on the PC in `test/host/`.
- `tools/sim/stubs/`: stand-ins for Arduino / M5 / RadioLib / BLE used by the simulator.
- `tools/pc/`: PC control panel (`hamos.html`, `hamos_pc.py`); after changing the page run
  `python3 tools/pc/embed.py` (embeds it in the firmware as `src/core/WebPage.h`).

## Conventions
- Screen text in English wrapped in `T("...")`; Turkish in `src/core/LangTr.inc`
  (same length or shorter; a footer holds about 39 characters).
- Colours always `uint16_t` (RGB565); Arduino macros (`word`, `bit`, `PI`, `min`...) must not be used as names.
- Library versions: M5Cardputer ^1.1.0, RadioLib 7.1.2, arduino-esp32 2.0.x (built-in BLE library).
- No model names or personal data in code or commits.
