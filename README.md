# Cardputer HamOS

An amateur radio toolkit for the M5Stack Cardputer ADV (should also run on the original Cardputer): one firmware, many small apps behind a launcher.

## Status

**Not yet built against the real M5Cardputer / RadioLib libraries or flashed on hardware.** Everything below runs on a PC in a simulator with fake GPS, LoRa and network data, which is where the screenshots come from (the font on the device differs slightly). Expect a round of fixes on the first real build.

![HamOS screens](docs/screens/overview.png)

| App | What it does | Needs |
|---|---|---|
| Logbook | Logs QSOs to a standard ADIF file. Shows country, distance and beam heading while you type the call, flags new countries and dupes, fills in band and UTC. `D` deletes a contact. | SD card |
| Mesh | Meshtastic-compatible chat, node list (signal, hops, distance, bearing) and radio status | LoRa cap |
| DX Cluster | Live telnet spots with band filter; countries you never logged are highlighted and beep | WiFi |
| Solar | Solar flux, A/K index and HF band conditions from hamqsl.com; sunrise/sunset for your grid | WiFi (sun times work offline) |
| GPS | Position, grid locator, altitude, speed; sets the clock without internet | LoRa cap |
| Locator | Distance and beam heading between two locators; "from" follows the GPS | |
| Calculator | Antenna length, SWR, dBm/Watt, feedline loss, LC resonance, Ohm's law | |
| Morse | Type-to-send practice and a Koch-method trainer that levels up at 90 % | |
| UTC Clock | UTC and local time (NTP or GPS) | |
| Reference | Q codes, phonetic alphabet, RST, band edges | |
| Settings | Everything below, editable on the device | |

The status bar shows UTC time, unread mesh messages (`M3`), GPS fix, WiFi and battery on every screen.

### Mesh (Meshtastic)

HamOS speaks the Meshtastic radio protocol directly, so it talks to ordinary Meshtastic nodes and phones. Defaults match a factory-fresh node: region `EU_868` (869.525 MHz), preset `LongFast`, the public default key.

- Works: channel text messages both ways, node names, positions, SNR/RSSI/hop count, replies to node-info requests, acknowledging messages addressed to you. Your own message turns green once another node is heard repeating it.
- Private channel: set `Mesh channel` and `Mesh key` to the name and base64 key shown in the Meshtastic app.
- Not implemented: relaying other nodes' packets (HamOS announces itself as `CLIENT_MUTE`), direct messages with the newer public-key encryption, telemetry, multiple channels at once.
- Airtime is counted and transmission pauses at the regional duty-cycle limit (10 % per hour in the EU).
- Position sharing is **off** by default; when on, it is sent coarsened to about 3 km.
- This is licence-free ISM-band operation, not amateur radio: the encryption used here is not allowed on amateur frequencies.

The packet format was checked against the official Meshtastic Python library (same bytes, decrypts with the same key); it has not been tested on air.

### LoRa + GNSS cap wiring

Taken from the Meshtastic firmware's Cardputer ADV board definition and the `espp` Cardputer component:

| Signal | GPIO |
|---|---|
| SX1262 NSS / RST / DIO1 / BUSY | 5 / 3 / 4 / 6 |
| SPI SCK / MISO / MOSI (shared with microSD) | 40 / 39 / 14 |
| GNSS to ESP32 / ESP32 to GNSS, 115200 baud | 15 / 13 |
| I/O expander (Cap LoRa-1262 only), I2C 0x43 | SDA 8, SCL 9; P0 antenna switch, P1 GNSS LNA |

The radio is started with a 1.8 V TCXO and falls back to a plain crystal if that fails; `lora_tcxo=` in `config.txt` overrides the voltage.

### Web interface

Set `Web UI` to `1` in Settings; the address then appears at the bottom of the Settings screen. From a phone or PC on the same WiFi you can download the log, read and send mesh messages and edit settings. It is plain HTTP: set a `Web UI password` (user `hamos`) on shared networks.

### Files on the SD card

| File | Content |
|---|---|
| `/HamOS/config.txt` | Settings as `key=value`. The WiFi password and mesh key are stored in plain text. |
| `/HamOS/log.adi` | The logbook (ADIF). `log.bak` is the copy from before the last delete. |
| `/HamOS/mesh.txt` | Every mesh message sent or received |

HamOS boots without a card, but nothing is saved.

### Known limits

- The list and the dupe check cover the newest 200 contacts; the new-country check covers the whole log.
- The country table has about 150 common entities, not the full DXCC list, and its coordinates are country centres.
- A QSO cannot be edited on the device, only deleted and re-entered.
- Loading solar data and connecting to the cluster block the keyboard for a few seconds.

## Adding an app

1. Create `src/apps/MyApp.h` with a class deriving from `App` (`src/core/App.h`).
2. Add an instance to the app registry at the top of `src/main.cpp`.

Logic that does not need hardware goes in `src/lib/` so it can be tested on a PC:

```bash
g++ -std=c++17 -I src test/host/test_libs.cpp -o test/host/t.out && test/host/t.out
g++ -std=c++17 -I src test/host/test_maidenhead.cpp -o test/host/t.out && test/host/t.out
g++ -std=c++17 -I src test/host/test_radio.cpp -lcrypto -o test/host/t.out && test/host/t.out
```

## Simulator and screenshots

`tools/sim/run.sh` compiles the real app code against stand-in hardware headers, runs the script in `tools/sim/sim.cpp` (key presses, a fake GNSS receiver, fake mesh neighbours, a fake cluster and solar feed), checks results end to end and writes screenshots to `docs/screens/`. It needs `g++`, OpenSSL headers and Python with Pillow. Use it to check a new app without flashing.

## Roadmap

Done: core, logbook, calculators, Morse, GPS, Meshtastic, DX cluster, solar, web interface.

Next, roughly in this order:

1. First hardware build and fixes.
2. Field mode (POTA/SOTA/contest: serial numbers, rate, park reference).
3. LoRa-APRS and APRS-IS.
4. Satellite pass prediction.
5. Audio jack features: voice/CW memory keyer, CW decoder, SSTV receive.
6. Rig control (rigctld / CI-V), WSJT-X companion, USB keyboard macros.
7. Callsign lookup and log upload (QRZ, Club Log), OTA updates.
8. Repeater list, Turkish interface, power saving.
