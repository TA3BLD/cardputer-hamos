# Cardputer HamOS

An amateur radio toolkit for the M5Stack Cardputer ADV (should also run on the original Cardputer): one firmware, many small apps behind a launcher.

## Status

Phases 1 and 2 are written. **Not yet built against the real M5Cardputer library or flashed on hardware.** The code runs on a PC in a simulator (below), which is where these screenshots come from; the font on the device differs slightly.

![HamOS screens](docs/screens/overview.png)

| App | What it does |
|---|---|
| Logbook | Logs QSOs to a standard ADIF file (`/HamOS/log.adi`), warns about dupes, fills in UTC time and band |
| Locator | Distance and beam heading between two Maidenhead locators |
| Calculator | Antenna length, SWR, dBm/Watt, feedline loss, LC resonance, Ohm's law |
| Morse | Type-to-send practice and a Koch-method trainer that levels up at 90 % |
| UTC Clock | NTP-synced UTC and local time |
| Reference | Q codes, phonetic alphabet, RST, band edges |
| Settings | Callsign, grid, UTC offset, WiFi, CW speed and tone |

The status bar shows UTC time, WiFi state and battery level on every screen.

Known limits: the logbook keeps only the newest 200 QSOs in memory (the file keeps all of them), so the list and the dupe check cover those 200. QSOs cannot be edited or deleted on the device yet.

## Keys

| Key | Action |
|---|---|
| `;` / `.` | Up / down |
| `,` / `/` | Left / right |
| `ENTER` | Open / edit / save |
| `TAB` | Next field or page |
| `` ` `` (ESC) | Cancel edit / back to launcher |

## Build

```bash
pio run -t upload
```

Settings are stored on the microSD card (FAT32) in `/HamOS/config.txt`. HamOS boots without a card, but settings are then lost at power-off. The WiFi password is stored there in plain text.

## Adding an app

1. Create `src/apps/MyApp.h` with a class deriving from `App` (`src/core/App.h`).
2. Add an instance to the app registry at the top of `src/main.cpp`.

Logic that does not need hardware goes in `src/lib/` so it can be tested on a PC:

```bash
g++ -std=c++17 -I src test/host/test_libs.cpp -o test/host/t.out && test/host/t.out
g++ -std=c++17 -I src test/host/test_maidenhead.cpp -o test/host/t.out && test/host/t.out
```

## Simulator and screenshots

`tools/sim/run.sh` compiles the real app code against stand-in hardware headers, presses the keys scripted in `tools/sim/sim.cpp` and writes screenshots to `docs/screens/`. It needs `g++` and Python with Pillow. Use it to check a new app's layout without flashing.

## Roadmap

1. **Core** (done): launcher, status bar, settings, WiFi/NTP, SD storage.
2. **Offline** (done): logbook, calculators, Morse trainer. Still open: repeater list, CW decoder.
3. **Online:** DX cluster, propagation, APRS-IS, satellite passes.
4. **PC bridge:** MQTT feed from WSJT-X / SDR, rig control, USB HID macros.
5. **Hardware add-ons:** GPS, LoRa, CW paddle, CAT.
