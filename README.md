# Cardputer HamOS

An amateur radio toolkit for the M5Stack Cardputer ADV (should also run on the original Cardputer): one firmware, many small apps behind a launcher.

## Status

Phase 1 skeleton. **Not yet built or flashed on real hardware.**

| App | What it does |
|---|---|
| Locator / Bearing | Distance and beam heading between two Maidenhead locators |
| UTC Clock | NTP-synced UTC and local time |
| Reference | Q codes, phonetic alphabet, RST, band edges |
| Settings | Callsign, grid, UTC offset, WiFi |

The status bar shows UTC time, WiFi state and battery level on every screen.

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
g++ -std=c++17 -I src test/host/test_maidenhead.cpp -o test/host/t.out && test/host/t.out
```

## Roadmap

1. **Core** (this phase): launcher, status bar, settings, WiFi/NTP, SD storage.
2. **Offline:** logbook with ADIF export, calculators, Morse trainer, repeater list.
3. **Online:** DX cluster, propagation, APRS-IS, satellite passes.
4. **PC bridge:** MQTT feed from WSJT-X / SDR, rig control, USB HID macros.
5. **Hardware add-ons:** GPS, LoRa, CW paddle, CAT.
