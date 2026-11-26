#include "RangeTest.h"
#include <Arduino.h>
#include <ctime>
#include "Config.h"
#include "Mesh.h"
#include "Net.h"
#include "Station.h"
#include "Storage.h"

namespace rangetest {

namespace {

constexpr int kPort = 66;           // RANGE_TEST_APP
constexpr const char* kCsv = "/HamOS/rangetest.csv";
Role current = Role::Off;
uint32_t seq = 0, lastSendMs = 0, sent = 0;
std::vector<Hit> list;
int gaps = 0;
uint32_t lastSeqFrom = 0, lastSeq = 0;

void received(uint32_t from, uint32_t, const std::string& payload, float snr, float rssi) {
    if (current != Role::Receive) return;
    if (payload.compare(0, 4, "seq ") != 0) return;
    Hit h;
    h.from = from;
    h.seq = (uint32_t)strtoul(payload.c_str() + 4, nullptr, 10);
    h.snr = snr;
    h.rssi = rssi;
    h.ms = millis();
    h.havePos = station::position(h.lat, h.lon);
    for (const mesh::Node& n : mesh::nodes())
        if (n.id == from && n.hasPosition && h.havePos) h.km = maidenhead::distanceKm(h.lat, h.lon, n.lat, n.lon);
    if (from == lastSeqFrom && h.seq > lastSeq + 1 && h.seq - lastSeq < 1000) gaps += (int)(h.seq - lastSeq - 1);
    lastSeqFrom = from;
    lastSeq = h.seq;
    list.push_back(h);
    if (list.size() > 300) list.erase(list.begin());
    char line[160];
    time_t t = net::timeValid() ? (time_t)(net::utcMillis() / 1000) : 0;
    struct tm g;
    gmtime_r(&t, &g);
    char stamp[24] = "";
    if (t) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &g);
    snprintf(line, sizeof(line), "%s,%s,%u,%.1f,%.0f,%.6f,%.6f,%.2f\n", stamp, mesh::nodeName(from).c_str(), (unsigned)h.seq, snr, rssi,
             h.havePos ? h.lat : 0.0, h.havePos ? h.lon : 0.0, h.km);
    if (!storage::exists(kCsv)) storage::appendFile(kCsv, "utc,from,seq,snr,rssi,lat,lon,km\n");
    storage::appendFile(kCsv, line);
}

}  // namespace

void begin() { mesh::onPort(kPort, received); }

void setRole(Role r) {
    current = r;
    lastSendMs = 0;
    if (r == Role::Receive) {
        list.clear();
        gaps = 0;
        lastSeqFrom = lastSeq = 0;
    }
}

Role role() { return current; }

int intervalSeconds() {
    int s = atoi(config::get("range_interval", "30").c_str());
    return s < 15 ? 15 : s;             // the mesh's duty cycle allows no more
}

void tick() {
    if (current != Role::Send || !mesh::ready()) return;
    uint32_t now = millis();
    if (lastSendMs && now - lastSendMs < (uint32_t)intervalSeconds() * 1000) return;
    lastSendMs = now ? now : 1;
    if (mesh::sendPort(kPort, "seq " + std::to_string(++seq))) sent++;
}

uint32_t sentCount() { return sent; }
const std::vector<Hit>& hits() { return list; }
int lost() { return gaps; }

double bestKm() {
    double best = 0;
    for (const Hit& h : list)
        if (h.km > best) best = h.km;
    return best;
}

}  // namespace rangetest
