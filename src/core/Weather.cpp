#include "Weather.h"
#include <Arduino.h>
#include <cmath>
#include "AprsIs.h"
#include "Cap.h"
#include "Config.h"
#include "Gps.h"
#include "Lang.h"
#include "LoraAprs.h"
#include "Station.h"
#include "lib/Aprs.h"
#include "lib/Bme280.h"

#ifdef HAMOS_SIM
double sim_wx_celsius = 18.4, sim_wx_humidity = 63, sim_wx_pascal = 100120;
#else
#include <Wire.h>
#endif

namespace weather {

namespace {

bool on = false, found = false, haveData = false;
#ifndef HAMOS_SIM
uint8_t address = bme280::kAddress;
bme280::Calibration cal;
#endif
bme280::Reading last;
uint32_t lastReadMs = 0, lastBeacon = 0;
std::string statusText;

#ifndef HAMOS_SIM
bool readRegs(uint8_t reg, uint8_t* out, size_t n) {
    Wire1.beginTransmission(address);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom((int)address, (int)n) != (int)n) return false;
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)Wire1.read();
    return true;
}
bool writeReg(uint8_t reg, uint8_t v) {
    Wire1.beginTransmission(address);
    Wire1.write(reg);
    Wire1.write(v);
    return Wire1.endTransmission() == 0;
}
#endif

bool probe() {
#ifdef HAMOS_SIM
    return true;
#else
    Wire1.begin(cap::GROVE_2, cap::GROVE_1, 100000);
    for (uint8_t a : {bme280::kAddress, bme280::kAddressAlt}) {
        address = a;
        uint8_t id = 0;
        if (readRegs(bme280::kRegId, &id, 1) && id == bme280::kChipId) {
            uint8_t c1[26], c2[7];
            if (!readRegs(bme280::kRegCalib1, c1, 26) || !readRegs(bme280::kRegCalib2, c2, 7)) return false;
            cal = bme280::parseCalibration(c1, c2);
            writeReg(bme280::kRegCtrlHum, 0x01);    // humidity x1
            writeReg(bme280::kRegConfig, 0x00);
            return true;
        }
    }
    return false;
#endif
}

bool read() {
#ifdef HAMOS_SIM
    last.celsius = sim_wx_celsius;
    last.humidity = sim_wx_humidity;
    last.pascal = sim_wx_pascal;
    return true;
#else
    if (!writeReg(bme280::kRegCtrlMeas, 0x25)) return false;   // temperature x1, pressure x1, one measurement
    delay(12);
    uint8_t raw[8];
    if (!readRegs(bme280::kRegData, raw, 8)) return false;
    last = bme280::compensate(cal, raw);
    return last.pascal > 30000;
#endif
}

double altitudeM() {
    std::string a = config::get("wx_alt");
    if (!a.empty()) return atof(a.c_str());
    if (gps::hasFix()) return gps::fix().altitudeM;
    return 0;
}

}  // namespace

void begin() {
    on = config::get("grove_io") == "wx";
    found = haveData = false;
    lastReadMs = 0;
    statusText = on ? T("Looking for the BME280...") : T("Set Grove = wx in Settings");
    if (!on) return;
    found = probe();
    if (!found) statusText = T("No BME280 on the Grove port");
}

void tick() {
    if (!found) return;
    uint32_t now = millis();
    if (lastReadMs && now - lastReadMs < 60000) {
        int minutes = atoi(config::get("wx_beacon", "0").c_str());
        if (minutes > 0 && haveData && (!lastBeacon || now - lastBeacon > (uint32_t)(minutes < 5 ? 5 : minutes) * 60000)) {
            std::string e;
            beaconNow(e);
        }
        return;
    }
    lastReadMs = now ? now : 1;
    haveData = read();
    statusText = haveData ? T("BME280 working") : T("BME280 read failed");
}

bool present() { return found; }
bool valid() { return haveData; }
double celsius() { return last.celsius; }
double humidity() { return last.humidity; }
double pressureHpa() { return last.pascal / 100.0; }
double seaLevelHpa() {
    double h = altitudeM();
    return pressureHpa() * std::pow(1.0 - 0.0065 * h / (last.celsius + 0.0065 * h + 273.15), -5.257);
}
std::string status() { return statusText; }
uint32_t lastBeaconMs() { return lastBeacon; }

bool beaconNow(std::string& error) {
    if (!haveData) { error = T("No weather data yet"); return false; }
    double lat, lon;
    if (!station::position(lat, lon)) { error = T("Set your grid locator or wait for GPS"); return false; }
    std::string call = config::get("callsign");
    if (call.empty()) { error = T("Set your callsign in Settings"); return false; }
    std::string ssid = config::get("wx_ssid", "13");
    std::string from = call + (ssid.empty() || ssid == "0" ? "" : "-" + ssid);
    std::string info = aprs::formatWeather(lat, lon, last.celsius, last.humidity, seaLevelHpa(), " HamOS WX");
    bool sent = false;
    if (loraaprs::ready()) sent |= loraaprs::send(from + ">" + aprs::kToCall + ",WIDE1-1:" + info);
    if (aprsis::online()) sent |= aprsis::sendRaw(aprs::header(from) + info);
    lastBeacon = millis();
    if (!lastBeacon) lastBeacon = 1;
    if (!sent) error = T("Neither LoRa-APRS nor APRS-IS is on");
    return sent;
}

}  // namespace weather
