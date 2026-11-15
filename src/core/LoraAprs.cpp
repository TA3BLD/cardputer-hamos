#include "LoraAprs.h"
#include <Arduino.h>
#include <deque>
#include "AprsIs.h"
#include "Config.h"
#include "Lora.h"
#include "lib/Aprs.h"

namespace loraaprs {

static bool isEnabled = false, isReady = false;
static std::string statusText = "off";
static std::deque<std::string> queue;
static float rssi = 0, snr = 0;
static unsigned heardCount = 0;
static uint32_t lastBeaconMs = 0, lastRxMs = 0;
static bool beaconSent = false;
static std::deque<std::pair<std::string, uint32_t>> recent;     // digipeated: source + text, when
static unsigned digiCount = 0;

// Repeats packets by the WIDEn-N rules ("LoRa digi": 1 = WIDE1-1 fill-in, 2 = also WIDE2-n).
static void digipeat(const std::string& text) {
    std::string mode = config::get("lora_digi", "0");
    if (mode != "1" && mode != "2") return;
    std::string me = aprsis::myCall();
    std::string out = aprs::digipeat(text, me, mode == "2");
    if (out.empty()) return;
    aprs::Packet p;
    if (!aprs::parseTnc2(text, p)) return;
    std::string key = p.src + ">" + p.info;
    uint32_t now = millis();
    while (!recent.empty() && now - recent.front().second > 30000) recent.pop_front();
    for (const auto& r : recent)
        if (r.first == key) return;             // repeated in the last 30 s already
    recent.push_back({key, now});
    if (queue.size() < 6) {
        queue.push_front(out);
        digiCount++;
    }
}

static void onPacket(const uint8_t* data, size_t len, float r, float s) {
    std::string text((const char*)data, len);
    if (text.size() >= 3 && text[0] == '<' && (uint8_t)text[1] == 0xFF && text[2] == 0x01) text.erase(0, 3);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == 0)) text.pop_back();
    if (text.find('>') == std::string::npos || text.find(':') == std::string::npos) return;
    rssi = r;
    snr = s;
    heardCount++;
    lastRxMs = millis();
    aprsis::ingest(text, aprsis::LORA);
    aprsis::gate(text);
    digipeat(text);
}

void begin() {
    isReady = false;
    queue.clear();
    isEnabled = config::get("lora_mode", "mesh") == "aprs";
    if (!isEnabled) { statusText = "LoRa radio is in Meshtastic mode"; return; }
    lora::Settings s;
    s.freqMhz = atof(config::get("lora_aprs_freq", "433.775").c_str());
    s.bwKhz = 125;
    s.sf = 12;
    s.cr = 5;
    s.syncWord = 0x12;
    s.preamble = 8;
    int power = atoi(config::get("lora_power", "22").c_str());
    s.powerDbm = power > 22 ? 22 : power < -9 ? -9 : power;
    lora::onReceive(onPacket);
    if (!lora::begin(s)) { statusText = lora::error(); return; }
    isReady = true;
    beaconSent = false;
    char buf[48];
    snprintf(buf, sizeof(buf), "LoRa-APRS on %.3f MHz", s.freqMhz);
    statusText = buf;
}

void tick() {
    if (!isReady) return;
    lora::tick();
    uint32_t now = millis();
    int minutes = atoi(config::get("lora_aprs_beacon", "0").c_str());     // 0 = only on request
    if (minutes > 0) {
        if (minutes < 5) minutes = 5;           // a beacon takes about 3 s of air time
        if (!beaconSent || now - lastBeaconMs > (uint32_t)minutes * 60000) {
            std::string error;
            aprsis::sendBeacon(error);
            beaconSent = true;
            lastBeaconMs = now;
        }
    }
    // Leave a short gap after a received packet so a digipeater's repeat is not stepped on.
    if (queue.empty() || lora::busy() || now - lastRxMs < 1500) return;
    std::string packet = "<\xff\x01" + queue.front();
    if (lora::send((const uint8_t*)packet.data(), packet.size())) queue.pop_front();
}

bool send(const std::string& tnc2) {
    if (!isReady || queue.size() >= 6 || tnc2.size() > 240) return false;
    queue.push_back(tnc2);
    return true;
}

bool enabled() { return isEnabled; }
unsigned digipeated() { return digiCount; }
bool ready() { return isReady; }
std::string status() { return statusText; }
float lastRssi() { return rssi; }
float lastSnr() { return snr; }
unsigned heard() { return heardCount; }

}  // namespace loraaprs
