#include "Watch.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include <map>
#include "Config.h"
#include "Log.h"
#include "Net.h"
#include "Pota.h"
#include "lib/Adif.h"
#include "lib/Dxcc.h"
#include "lib/Watch.h"

namespace watch {

namespace {

std::vector<Hit> list;
std::map<std::string, uint32_t> recent;     // call + band -> when announced
std::string notice;
uint32_t lastPollMs = 0;

}  // namespace

void check(const std::string& call, double khz, const std::string& ref, const char* where, const std::string& info) {
    std::vector<std::string> patterns = watchlist::parse(config::get("watch"));
    if (patterns.empty() || call.empty()) return;
    bool hit = false;
    std::string why;
    for (const std::string& p : patterns) {
        if (p == "NEW") {
            const dxcc::Entity* e = dxcc::lookup(call);
            if (e && !logbook::workedEntity(e)) { hit = true; why = std::string("new ") + e->name; }
        } else if (watchlist::match(p, call) || (!ref.empty() && watchlist::match(p, ref))) {
            hit = true;
            why = p;
        }
        if (hit) break;
    }
    if (!hit) return;
    std::string key = call + "/" + adif::band(khz / 1000);
    uint32_t now = millis();
    auto it = recent.find(key);
    if (it != recent.end() && now - it->second < 30 * 60 * 1000) return;
    recent[key] = now;
    Hit h;
    h.call = call;
    h.where = where;
    h.info = ref.empty() ? info : ref + " " + info;
    h.khz = khz;
    h.ms = now;
    list.push_back(h);
    if (list.size() > 40) list.erase(list.begin());
    char b[96];
    snprintf(b, sizeof(b), "%s %.1f %s (%s)", call.c_str(), khz, where, why.c_str());
    notice = b;
    M5Cardputer.Speaker.tone(1500, 120);
}

const std::vector<Hit>& hits() { return list; }

std::string takeNotice() {
    std::string n;
    n.swap(notice);
    return n;
}

void tick(bool idle) {
    if (!idle || !net::connected() || config::get("watch").empty()) return;
    uint32_t now = millis();
    if (lastPollMs && now - lastPollMs < 3 * 60 * 1000) return;
    lastPollMs = now ? now : 1;
    std::string err;
    std::vector<pota::Spot> spots;
    if (pota::fetchSpots(pota::POTA, spots, err))
        for (const pota::Spot& s : spots) check(s.activator, s.freqKhz, s.ref, "POTA", s.mode);
}

}  // namespace watch
