#include "Wsjtx.h"
#include <M5Cardputer.h>
#include <WiFiUdp.h>
#include <cstdio>
#include "Config.h"
#include "Log.h"
#include "Net.h"
#include "lib/Adif.h"
#include "lib/Wsjtx.h"

namespace wsjtxlink {

static WiFiUDP udp;
static bool isEnabled = false, isListening = false, isTransmitting = false;
static std::vector<Decode> list;
static std::string dialText, dxCallText;
static uint32_t lastPacketMs = 0, loggedCount = 0;
static bool everHeard = false;

void begin() {
    if (isListening) udp.stop();
    isListening = false;
    isEnabled = config::get("wsjtx_on", "0") == "1";
}

static void handle(const wsjtx::Message& m) {
    lastPacketMs = millis();
    everHeard = true;
    if (m.type == wsjtx::STATUS) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f %s", m.dialHz / 1e6, m.mode.c_str());
        dialText = buf;
        dxCallText = m.dxCall;
        isTransmitting = m.transmitting;
    } else if (m.type == wsjtx::DECODE && m.isNew) {
        wsjtx::Decoded info = wsjtx::analyse(m.text);
        Decode d;
        d.snr = m.snr;
        d.df = m.df;
        d.text = m.text;
        d.from = info.from;
        d.cq = info.cq;
        std::string me = config::get("callsign");
        d.toMe = !me.empty() && info.to == me;
        d.entity = dxcc::lookup(info.from);
        d.newEntity = d.entity && !logbook::workedEntity(d.entity);
        list.push_back(d);
        if (list.size() > 40) list.erase(list.begin());
        if (d.toMe) M5Cardputer.Speaker.tone(1800, 120);            // someone is calling you
        else if (d.cq && d.newEntity && config::get("dx_beep", "1") == "1") M5Cardputer.Speaker.tone(2200, 60);
    } else if (m.type == wsjtx::LOGGED_ADIF) {
        for (adif::Qso q : adif::parse(m.adif)) {
            q.potaRef = config::get("pota_ref");
            q.sotaRef = config::get("sota_ref");
            logbook::add(q);
            loggedCount++;
        }
        M5Cardputer.Speaker.tone(1200, 80);
    }
}

void tick() {
    if (!isEnabled) return;
    if (!net::connected()) {
        if (isListening) { udp.stop(); isListening = false; }
        return;
    }
    if (!isListening) {
        logbook::load();
        isListening = udp.begin((uint16_t)atoi(config::get("wsjtx_port", "2237").c_str()));
        return;
    }
    uint8_t buf[1024];
    for (int budget = 8; budget > 0; budget--) {
        int size = udp.parsePacket();
        if (size <= 0) break;
        int n = udp.read(buf, sizeof(buf));
        wsjtx::Message m;
        if (n > 0 && wsjtx::parse(buf, (size_t)n, m)) handle(m);
    }
}

bool enabled() { return isEnabled; }
bool listening() { return isListening; }
bool active() { return everHeard && millis() - lastPacketMs < 30000; }
const std::vector<Decode>& decodes() { return list; }
std::string dial() { return dialText; }
std::string dxCall() { return dxCallText; }
bool transmitting() { return isTransmitting; }
uint32_t logged() { return loggedCount; }

}  // namespace wsjtxlink
