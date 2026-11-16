#include "Tracker.h"
#include <Arduino.h>
#include "AprsIs.h"
#include "Config.h"
#include "Gps.h"
#include "Lang.h"
#include "LoraAprs.h"
#include "Mesh.h"
#include "lib/SmartBeacon.h"

namespace tracker {

namespace {
bool on = false;
uint32_t lastMs = 0, count = 0;
double lastCourse = 0;
smartbeacon::Params params() {
    smartbeacon::Params p;
    p.fastSpeed = atof(config::get("sb_fast_kmh", "90").c_str());
    p.fastRate = atof(config::get("sb_fast_s", "60").c_str());
    p.slowSpeed = atof(config::get("sb_slow_kmh", "5").c_str());
    p.slowRate = atof(config::get("sb_slow_s", "1800").c_str());
    if (p.fastRate < 30) p.fastRate = 30;                   // LoRa air time is limited
    return p;
}
}  // namespace

std::string via() {
    if (loraaprs::ready()) return "LoRa-APRS";
    if (aprsis::online()) return "APRS-IS";
    if (mesh::ready()) return "mesh";
    return T("Neither LoRa nor APRS-IS is on");
}

void start() {
    on = true;
    lastMs = 0;
}
void stop() { on = false; }
bool running() { return on; }
uint32_t sent() { return count; }

double secondsToNext() {
    if (!on || !gps::hasFix()) return -1;
    double r = smartbeacon::rate(params(), gps::fix().speedKmh) - (millis() - lastMs) / 1000.0;
    return r < 0 ? 0 : r;
}

void tick() {
    if (!on || !gps::hasFix()) return;
    uint32_t now = millis();
    const nmea::Fix& f = gps::fix();
    double since = lastMs ? (now - lastMs) / 1000.0 : 1e9;
    if (!smartbeacon::due(params(), since, f.speedKmh, f.courseDeg, lastCourse)) return;
    bool ok = false;
    if (loraaprs::ready() || aprsis::online()) {
        std::string err;
        ok = aprsis::sendBeacon(err);
    } else if (mesh::ready()) {
        ok = mesh::sendPosition();
    }
    lastMs = now ? now : 1;
    lastCourse = f.courseDeg;
    if (ok) count++;
}

}  // namespace tracker
