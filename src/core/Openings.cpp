#include "Openings.h"
#include <Arduino.h>
#include <algorithm>
#include "Config.h"
#include "Http.h"
#include "Lang.h"
#include "Net.h"
#include "Station.h"
#include "lib/Adif.h"
#include "lib/Dxcc.h"
#include "lib/MiniXml.h"

namespace openings {

namespace {

constexpr double kFarKm = 2500;
constexpr int kOpenPaths = 3;
std::vector<Band> open;
std::set<std::string> lastOpen;
uint32_t lastMs = 0;
std::string notice;

struct Parse {
    std::string tail;                   // part of an element not complete yet
    std::string grid;
    double lat = 0, lon = 0;
    std::map<std::string, Band> acc;
    std::set<std::string> seen;
};

void element(Parse& p, const std::string& el) {
    std::vector<xml::Attrs> v = xml::elements(el, "receptionReport");
    if (v.empty()) return;
    xml::Attrs& a = v[0];
    std::string rxLoc = a["receiverLocator"], txLoc = a["senderLocator"];
    auto mine = [&](const std::string& loc) {
        if (loc.size() < 4) return false;
        std::string g = loc.substr(0, 4);
        for (char& c : g) c = (char)toupper((unsigned char)c);
        return g == p.grid;
    };
    uint32_t f = (uint32_t)strtoul(a["frequency"].c_str(), nullptr, 10);
    if (mine(rxLoc)) addReport(p.acc, p.seen, a["senderCallsign"], txLoc, f, p.lat, p.lon);
    else if (mine(txLoc)) addReport(p.acc, p.seen, a["receiverCallsign"], rxLoc, f, p.lat, p.lon);
}

}  // namespace

void addReport(std::map<std::string, Band>& acc, std::set<std::string>& seen, const std::string& farCall, const std::string& farLocator,
               uint32_t freqHz, double myLat, double myLon) {
    double lat, lon;
    if (!maidenhead::toLatLon(farLocator, lat, lon)) return;
    double km = maidenhead::distanceKm(myLat, myLon, lat, lon);
    if (km < kFarKm) return;
    std::string band = adif::band(freqHz / 1e6);
    if (band.empty() || !seen.insert(band + "/" + farCall).second) return;
    Band& b = acc[band];
    b.name = band;
    b.paths++;
    b.farthestKm = std::max(b.farthestKm, km);
    if (const dxcc::Entity* e = dxcc::lookup(farCall))
        if (e->continent) b.continents.insert(e->continent);
}

bool check(std::string& error) {
    Parse p;
    std::string grid = station::grid();
    if (grid.size() < 4 || !station::position(p.lat, p.lon)) {
        error = T("Set your grid locator or wait for GPS");
        return false;
    }
    p.grid = grid.substr(0, 4);
    for (char& c : p.grid) c = (char)toupper((unsigned char)c);
    http::Request r;
    r.url = "https://retrieve.pskreporter.info/query?callsign=" + p.grid + "&modify=grid&flowStartSeconds=-900&rronly=1&appcontact=HamOS";
    r.timeoutMs = 25000;
    http::Response res = http::stream(
            r,
            [](const uint8_t* d, size_t n, void* ctx) {
                Parse& p = *(Parse*)ctx;
                p.tail.append((const char*)d, n);
                size_t start;
                while ((start = p.tail.find("<receptionReport ")) != std::string::npos) {
                    size_t end = p.tail.find("/>", start);
                    if (end == std::string::npos) break;
                    element(p, p.tail.substr(start, end + 2 - start));
                    p.tail.erase(0, end + 2);
                }
                if (p.tail.size() > 4096) p.tail.erase(0, p.tail.size() - 1024);
                return true;
            },
            &p);
    lastMs = millis();
    if (!res.ok()) {
        error = res.status == 503 ? T("PSK Reporter is busy, try again in a few minutes") : res.error;
        return false;
    }
    open.clear();
    std::set<std::string> nowOpen;
    for (auto& kv : p.acc)
        if (kv.second.paths >= kOpenPaths) {
            open.push_back(kv.second);
            nowOpen.insert(kv.first);
        }
    std::sort(open.begin(), open.end(), [](const Band& a, const Band& b) { return a.paths > b.paths; });
    for (const Band& b : open)
        if (!lastOpen.count(b.name)) {
            std::string c;
            for (const std::string& x : b.continents) c += " " + x;
            notice = b.name + " " + T("open:") + c;
            break;
        }
    lastOpen = nowOpen;
    return true;
}

void tick(bool idle) {
    if (!idle || config::get("band_alert", "0") != "1" || !net::connected()) return;
    uint32_t now = millis();
    if (lastMs && now - lastMs < 15 * 60 * 1000) return;
    std::string e;
    check(e);
    if (!lastMs) lastMs = 1;
}

const std::vector<Band>& bands() { return open; }
uint32_t lastCheckMs() { return lastMs; }
std::string takeNotice() {
    std::string n;
    n.swap(notice);
    return n;
}

}  // namespace openings
