# Cardputer HamOS

An amateur radio toolkit for the M5Stack Cardputer ADV (with or without the LoRa + GNSS cap): one firmware, many small apps behind a launcher.

## Status

**Not yet built with PlatformIO or flashed on hardware.** Everything below runs on a PC in a simulator with fake GPS, LoRa, audio and network data; that is where the screenshots come from (the font on the device differs slightly). Expect a round of fixes on the first real build. See [What has been verified](#what-has-been-verified).

![HamOS screens](docs/screens/overview.png)

| App | What it does | Needs |
|---|---|---|
| Logbook | QSOs in a standard ADIF file. Country, distance and beam heading while you type the call; new countries and dupes flagged; band and UTC filled in; frequency and mode taken from the radio if rig control is set up. POTA reference and contest serial numbers optional. | SD card |
| Mesh | Meshtastic-compatible chat, node list (signal, hops, distance, bearing), radio status | LoRa cap |
| APRS | Nearby stations and two-way messages over APRS-IS; 1200 baud packet modem that decodes from the microphone and plays your beacon through the speaker | WiFi / a radio |
| DX Cluster | Live telnet spots with band filter; countries you never logged are highlighted and beep | WiFi |
| FT8 | Second screen for WSJT-X / JTDX on a PC: decodes, stations calling you, CQs from new countries; contacts logged there go into the HamOS log | WiFi + PC |
| Solar | Solar flux, A/K index and HF band conditions from hamqsl.com; sunrise/sunset for your grid | WiFi (sun times offline) |
| Satellites | Live azimuth/elevation/range and Doppler, next passes over your location | WiFi once, for orbit data |
| GPS | Position, grid locator, altitude, speed; sets the clock without internet | LoRa cap |
| Decoder | Morse code (automatic speed) and RTTY (45 baud, 170 Hz) from the microphone; sends an RTTY CQ through the speaker | a receiver nearby |
| Locator | Distance and beam heading between two locators; "from" follows the GPS | |
| Repeaters | Your repeater list from the SD card, nearest first | SD card |
| Calculator | Antenna length, SWR, dBm/Watt, feedline loss, LC resonance, Ohm's law | |
| Morse | Type-to-send practice and a Koch-method trainer that levels up at 90 % | |
| UTC Clock | UTC and local time (NTP or GPS) | |
| Reference | Q codes, phonetic alphabet, RST, band edges | |
| Settings | Everything, editable on the device | |

The status bar shows UTC time, unread mesh (`M3`) and APRS (`A1`) messages, GPS fix, WiFi and battery on every screen. The screen dims after a minute without key presses.

## Keys

| Key | Action |
|---|---|
| `;` `.` `,` `/` (with or without Fn) | Up / down / left / right |
| `ENTER` | Open / edit / save / send |
| `TAB` | Next field or page |
| `` ` `` or Fn+`` ` `` (ESC) | Cancel / back to the launcher |
| Fn + `;` / `.` | Scroll, on screens where `;` and `.` are text (mesh chat) |

## Build

```bash
pio run -t upload
```

`platformio.ini` pins RadioLib to 7.1.2 (its development branch has a different `begin()`), and needs M5Cardputer 1.1.0 or newer for the ADV. HamOS builds with both layouts of that library's keyboard state (1.1.1 moved Backspace, Esc and the arrows into new fields).

## Apps in detail

### Mesh (Meshtastic)

HamOS speaks the Meshtastic radio protocol directly, so it talks to ordinary Meshtastic nodes and phones. Defaults match a factory-fresh node: region `EU_868` (869.525 MHz), preset `LongFast`, the public default key.

- Works: channel text messages both ways, node names, positions, SNR/RSSI/hop count, replies to node-info requests, acknowledging messages addressed to you. Your own message turns green once another node is heard repeating it.
- Private channel: set `Mesh channel` and `Mesh key` to the name and base64 key shown in the Meshtastic app.
- Not implemented: relaying other nodes' packets (HamOS announces itself as `CLIENT_MUTE`), direct messages with the newer public-key encryption, telemetry, several channels at once.
- Airtime is counted and transmission pauses at the regional duty-cycle limit (10 % per hour in the EU).
- Position sharing is **off** by default; when on, it is sent coarsened to about 3 km.
- This is licence-free ISM-band operation, not amateur radio: the encryption used here is not allowed on amateur frequencies.

### APRS

- **APRS-IS** (set `APRS-IS` to 1): logs in as `CALL-7` with the standard passcode, shows stations within `APRS range` km sorted by distance, receives messages addressed to you (and acknowledges them), sends messages (`CALLSIGN text` then ENTER; green once acknowledged). `B` on the station page sends your position; `APRS beacon` repeats it every 20 minutes. Requires an amateur licence, like any APRS-IS transmit login.
- **Radio modem** (third page): decodes 1200 baud AX.25 packets from the microphone, so any receiver on 144.800 MHz held next to the Cardputer works. `B` plays your position packet through the speaker for a transmitter with VOX. Acoustic coupling is the weak point here; this part is experimental until tried on a real radio.
- Position formats decoded: plain and compressed. Mic-E packets are not decoded.
- LoRa-APRS is not included: it lives on 433.775 MHz and the cap's radio and antenna are built for 868 MHz.

### Decoder

Hold the Cardputer next to the receiver's speaker.

- **CW:** tone detection at 700 Hz (`,` and `/` move it in 50 Hz steps), speed followed automatically (tested from 12 to 28 WPM). Plain noise is not turned into letters.
- **RTTY:** 45.45 baud, 170 Hz shift, mark 2125 Hz. `R` swaps mark and space. `ENTER` plays "CQ CQ CQ DE your-call" through the speaker.
- PSK31 and SSTV are not implemented.

### Satellites

Press `U` once with WiFi to download the amateur satellite orbit data from celestrak.org; it is saved to the SD card and works offline afterwards (refresh every week or two). Open a satellite for its live azimuth, elevation, range, Doppler shift on 2 m and 70 cm, and the next four passes with local rise time, peak elevation, duration and direction. Needs your position (grid or GPS) and the time. Only low orbits are calculated; geostationary satellites such as QO-100 are listed but not predicted.

### FT8 (WSJT-X companion)

Set `WSJT-X link` to 1. In WSJT-X or JTDX on the PC: Settings > Reporting > UDP Server = the address shown on the FT8 screen, port 2237. The screen then shows every decode with SNR, audio frequency and country; a message addressed to you is green and beeps, a CQ from a country not in your log is orange. `C` shows only CQ calls. When you log a contact in WSJT-X it is added to the HamOS logbook.

### Logbook extras

- **Rig control:** run Hamlib's `rigctld` for your radio on a PC or Raspberry Pi and put its address in `Rig host`. New QSOs then start with the radio's frequency and mode.
- **POTA:** `POTA park ref` (e.g. `TR-0042`) is written into every contact as `MY_SIG` / `MY_SIG_INFO`.
- **Contest mode:** the note field becomes the received number, your serial number counts up and is shown in the form, both are stored as `STX` / `SRX`; the list shows the rate over the last hour.
- **ID reminder:** `ID reminder min` beeps and shows a notice at that interval.

### Web interface

Set `Web UI` to 1; the address then appears at the bottom of the Settings screen. From a phone or PC on the same WiFi you can download the log, read and send mesh messages, edit settings, and install a new `firmware.bin`. It is plain HTTP: set a `Web UI password` (user `hamos`) on shared networks. Firmware update is only offered when a password is set.

## Hardware notes

LoRa + GNSS cap wiring, taken from the Meshtastic firmware's Cardputer ADV board definition and the `espp` Cardputer component:

| Signal | GPIO |
|---|---|
| SX1262 NSS / RST / DIO1 / BUSY | 5 / 3 / 4 / 6 |
| SPI SCK / MISO / MOSI (shared with microSD) | 40 / 39 / 14 |
| GNSS to ESP32 / ESP32 to GNSS, 115200 baud | 15 / 13 |
| I/O expander (Cap LoRa-1262 only), I2C 0x43 | SDA 8, SCL 9; P0 antenna switch, P1 GNSS LNA |

The radio is started with a 1.8 V TCXO and falls back to a plain crystal if that fails; `lora_tcxo=` in `config.txt` overrides the voltage. Mesh > Info shows which cap version was detected.

## Files on the SD card

| File | Content |
|---|---|
| `/HamOS/config.txt` | Settings as `key=value`. The WiFi password and mesh key are stored in plain text. |
| `/HamOS/log.adi` | The logbook (ADIF). `log.bak` is the copy from before the last delete. |
| `/HamOS/mesh.txt` | Every mesh message sent or received |
| `/HamOS/tle.txt` | Satellite orbit data |
| `/HamOS/repeaters.csv` | Your repeater list: `name,MHz,shift,tone,lat,lon,note` (see `docs/repeaters-example.csv`) |

HamOS boots without a card, but nothing is saved.

## Known limits

- The list and the dupe check cover the newest 200 contacts; the new-country check covers the whole log.
- The country table has about 150 common entities, not the full DXCC list, and its coordinates are country centres.
- A QSO cannot be edited on the device, only deleted and re-entered.
- Loading solar or orbit data and connecting to the cluster, APRS-IS or rigctld block the keyboard for up to a few seconds.
- While a decoder page is open the speaker is off (microphone and speaker share one audio interface), so message beeps are silent there.
- No repeater data is included; the example file contains made-up entries.

## What has been verified

On a PC, without hardware:

- **Unit tests** (`test/host/`): locator maths, ADIF, radio formulas, Morse, NMEA, sunrise, DX spots, country lookup, solar feed, Meshtastic packets and encryption, WSJT-X messages, APRS formats, AX.25, and the modems.
- **Against reference implementations:** Meshtastic packets decrypt and parse with the official Meshtastic Python library and are byte-identical to its encoder; satellite positions match the Python `sgp4` package to within a metre and look angles match `skyfield`.
- **Modems with synthesized audio:** AFSK 1200, RTTY and CW decode clean, quiet, noisy and offset signals; CW at 12, 20 and 28 WPM; noise alone decodes to nothing.
- **Simulator** (`tools/sim/`): the real app code runs against stand-in hardware and is driven by a script; it checks results end to end (mesh send/receive, APRS login, messages and acknowledgements, packets played through the speaker decoded again, log deletion, WSJT-X logging, keyboard handling) and renders the screenshots.
- **Library interfaces:** the calls into M5Cardputer, M5Unified, RadioLib 7.1.2 and the Arduino-ESP32 network classes were checked against those projects' header files. The sources were also compiled with Arduino's common macros defined to catch name collisions.

Not verified: an actual PlatformIO build, anything on air, real microphone and speaker levels, the cap's TCXO voltage, the default DX cluster address, the WSJT-X message layout against a running WSJT-X.

## Adding an app

1. Create `src/apps/MyApp.h` with a class deriving from `App` (`src/core/App.h`).
2. Add an instance to the app registry at the top of `src/main.cpp`.

Logic that does not need hardware goes in `src/lib/` so it can be tested on a PC:

```bash
for t in test_libs test_maidenhead test_radio test_modem test_sat; do
  g++ -std=c++17 -O1 -I src test/host/$t.cpp -lcrypto -o test/host/t.out && test/host/t.out
done
```

`tools/sim/run.sh` builds and runs the simulator and rewrites `docs/screens/`. It needs `g++`, OpenSSL headers and Python with Pillow.

## Not done yet

- SSTV receive, PSK31
- USB keyboard macros for PC logging programs
- Callsign lookup and log upload (QRZ, Club Log): these need an account and API key
- Turkish interface, CW paddle keyer
