#include "PskReporter.h"
#include <Arduino.h>
#include <WiFiUdp.h>
#include <algorithm>
#include <map>
#include "Config.h"
#include "Http.h"
#include "Lang.h"
#include "Net.h"
#include "Station.h"
#include "Version.h"
#include "lib/Adif.h"
#include "lib/Dxcc.h"
#include "lib/MiniXml.h"
#include "lib/PskReport.h"

namespace pskr {

namespace {

constexpr uint32_t kSendEveryMs = 5 * 60 * 1000;
constexpr uint32_t kDupSeconds = 30 * 60;
constexpr uint32_t kTemplatesEverySeconds = 60 * 60;

std::vector<pskreport::Spot> queue;
std::map<std::string, uint32_t> lastReported;      // call + band -> unix time
uint32_t lastSendMs = 0, sequence = 0, domainId = 0, lastTemplates = 0, packetsWithTemplates = 0;
size_t total = 0;

std::vector<Heard> lastHeard;
uint32_t lastQueryMs = 0;
bool queried = false;

uint32_t nowUnix() { return net::timeValid() ? (uint32_t)(net::utcMillis() / 1000) : 0; }

void send() {
    std::string call = config::get("callsign"), grid = station::grid();
    if (call.empty() || grid.size() < 4 || queue.empty()) return;
    if (!domainId) domainId = esp_random() | 1;
    WiFiUDP udp;
    uint32_t now = nowUnix();
    // Templates go with the first few datagrams, then once an hour.
    if (now - lastTemplates > kTemplatesEverySeconds) packetsWithTemplates = 3;
    size_t first = 0;
    std::vector<uint8_t> dgram;
    while (first < queue.size()) {
        bool templates = packetsWithTemplates > 0;
        size_t n = pskreport::build(dgram, queue, first, templates, call, grid, std::string("HamOS ") + HAMOS_VERSION, now,
                                    sequence, domainId);
        if (!n) break;
        if (!udp.beginPacket("report.pskreporter.info", 4739)) return;
        udp.write(dgram.data(), dgram.size());
        if (!udp.endPacket()) return;
        if (templates) {
            packetsWithTemplates--;
            lastTemplates = now;
        }
        sequence += (uint32_t)n;
        total += n;
        first += n;
    }
    queue.erase(queue.begin(), queue.begin() + (long)first);
}

}  // namespace

void add(const std::string& call, const std::string& locator, const char* mode, uint32_t freqHz, int snr) {
    if (config::get("psk_report", "0") != "1" || call.empty() || call[0] == '<' || !freqHz) return;
    uint32_t now = nowUnix();
    if (!now) return;                       // reports need the right time
    std::string key = call + "/" + adif::band(freqHz / 1e6);
    auto it = lastReported.find(key);
    if (it != lastReported.end() && now - it->second < kDupSeconds) return;
    lastReported[key] = now;
    if (lastReported.size() > 500) {
        for (auto i = lastReported.begin(); i != lastReported.end();)
            i = now - i->second >= kDupSeconds ? lastReported.erase(i) : std::next(i);
    }
    if (queue.size() >= 200) queue.erase(queue.begin());
    pskreport::Spot s;
    s.call = call;
    s.locator = locator;
    s.mode = mode;
    s.freqHz = freqHz;
    s.snr = snr;
    s.unixTime = now;
    queue.push_back(s);
}

void tick() {
    if (queue.empty() || !net::connected()) return;
    uint32_t ms = millis();
    if (lastSendMs && ms - lastSendMs < kSendEveryMs) return;
    lastSendMs = ms ? ms : 1;
    send();
}

size_t queued() { return queue.size(); }
size_t sentTotal() { return total; }

bool heardMe(std::vector<Heard>& out, std::string& error, int minutes) {
    uint32_t ms = millis();
    if (queried && ms - lastQueryMs < 5 * 60 * 1000) {
        out = lastHeard;
        return true;
    }
    std::string call = config::get("callsign");
    if (call.empty()) {
        error = T("Set your callsign in Settings");
        return false;
    }
    http::Request r;
    r.url = "https://retrieve.pskreporter.info/query?senderCallsign=" + http::urlEncode(call) +
            "&flowStartSeconds=-" + std::to_string(minutes * 60) + "&rronly=1&appcontact=HamOS";
    r.maxBytes = 96 * 1024;
    r.timeoutMs = 20000;
    http::Response a = http::fetch(r);
    if (!a.ok()) {
        error = a.status == 503 ? T("PSK Reporter is busy, try again in a few minutes") : a.error;
        return false;
    }
    queried = true;
    lastQueryMs = ms;
    double myLat = 0, myLon = 0;
    bool havePos = station::position(myLat, myLon);
    out.clear();
    for (const xml::Attrs& a2 : xml::elements(a.body, "receptionReport")) {
        auto get = [&](const char* k) {
            auto it = a2.find(k);
            return it == a2.end() ? std::string() : it->second;
        };
        Heard h;
        h.call = get("receiverCallsign");
        h.locator = get("receiverLocator");
        h.mode = get("mode");
        h.freqHz = (uint32_t)strtoul(get("frequency").c_str(), nullptr, 10);
        h.snr = atoi(get("sNR").c_str());
        h.unixTime = (uint32_t)strtoul(get("flowStartSeconds").c_str(), nullptr, 10);
        if (const dxcc::Entity* e = dxcc::lookup(h.call)) h.country = e->name;
        double lat, lon;
        if (havePos && maidenhead::toLatLon(h.locator, lat, lon)) {
            h.km = maidenhead::distanceKm(myLat, myLon, lat, lon);
            h.bearing = maidenhead::bearingDeg(myLat, myLon, lat, lon);
        }
        if (!h.call.empty()) out.push_back(h);
    }
    std::sort(out.begin(), out.end(), [](const Heard& x, const Heard& y) { return x.unixTime > y.unixTime; });
    lastHeard = out;
    return true;
}

}  // namespace pskr
