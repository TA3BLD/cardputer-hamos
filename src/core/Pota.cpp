#include "Pota.h"
#include <algorithm>
#include <ctime>
#include "Config.h"
#include "Http.h"
#include "Lang.h"
#include "Log.h"
#include "Net.h"
#include "Station.h"
#include "Storage.h"
#include "lib/Csv.h"
#include "lib/JsonDom.h"
#include "lib/MiniJson.h"

namespace pota {

namespace {

// "2026-11-14T12:34:56" (UTC) -> seconds since 1970
uint32_t parseTime(const std::string& s) {
    if (s.size() < 19) return 0;
    adif::Qso q;
    q.date = s.substr(0, 4) + s.substr(5, 2) + s.substr(8, 2);
    q.time = s.substr(11, 2) + s.substr(14, 2);
    return (uint32_t)(adif::startUnix(q) + atoi(s.substr(17, 2).c_str()));
}

}  // namespace

const char* nearbyPath(Program p) { return p == POTA ? "/HamOS/pota/parks.csv" : "/HamOS/pota/summits.csv"; }

bool fetchSpots(Program p, std::vector<Spot>& out, std::string& error) {
    http::Request r;
    r.url = p == POTA ? "https://api.pota.app/spot/activator" : "https://api2.sota.org.uk/api/spots/30/all";
    r.maxBytes = 160 * 1024;
    r.timeoutMs = 15000;
    http::Response a = http::fetch(r);
    if (!a.ok()) {
        error = a.error;
        return false;
    }
    jdom::Value v;
    if (!jdom::parse(a.body, v) || !v.isArray()) {
        error = T("Unexpected answer from the server");
        return false;
    }
    a.body.clear();
    a.body.shrink_to_fit();
    double myLat = 0, myLon = 0;
    bool havePos = station::position(myLat, myLon);
    out.clear();
    for (size_t i = 0; i < v.size(); i++) {
        const jdom::Value& s = v[i];
        Spot x;
        if (p == POTA) {
            x.activator = s["activator"].str();
            x.ref = s["reference"].str();
            x.name = s["name"].str();
            x.location = s["locationDesc"].str();
            x.mode = s["mode"].str();
            x.freqKhz = s["frequency"].num();
            x.comment = s["comments"].str();
            x.spotter = s["spotter"].str();
            x.unixTime = parseTime(s["spotTime"].str());
            double lat = s["latitude"].num(1000), lon = s["longitude"].num(1000);
            if (havePos && lat < 999 && lon < 999) x.km = maidenhead::distanceKm(myLat, myLon, lat, lon);
        } else {
            x.activator = s["activatorCallsign"].str();
            x.ref = s["associationCode"].str() + "/" + s["summitCode"].str();
            x.name = s["summitDetails"].str();
            x.mode = s["mode"].str();
            for (char& c : x.mode) c = (char)toupper((unsigned char)c);
            x.freqKhz = s["frequency"].num() * 1000;        // SOTA gives MHz
            x.comment = s["comments"].str();
            x.spotter = s["callsign"].str();
            x.unixTime = parseTime(s["timeStamp"].str());
        }
        if (!x.activator.empty()) out.push_back(x);
    }
    std::sort(out.begin(), out.end(), [](const Spot& a, const Spot& b) { return a.unixTime > b.unixTime; });
    return true;
}

bool selfSpot(const std::string& ref, double freqKhz, const std::string& mode, const std::string& comment, std::string& error) {
    std::string call = config::get("callsign");
    if (call.empty()) {
        error = T("Set your callsign in Settings");
        return false;
    }
    if (ref.empty()) {
        error = T("Set your park (POTA park ref) in Settings");
        return false;
    }
    char f[24];
    snprintf(f, sizeof(f), "%.1f", freqKhz);
    http::Request r;
    r.method = "POST";
    r.url = "https://api.pota.app/spot";
    r.contentType = "application/json";
    r.body = "{\"activator\":" + json::quote(call) + ",\"spotter\":" + json::quote(call) + ",\"frequency\":" + json::quote(f) + ",\"reference\":" + json::quote(ref) +
             ",\"mode\":" + json::quote(mode) + ",\"source\":\"HamOS\",\"comments\":" + json::quote(comment) + "}";
    http::Response a = http::fetch(r);
    if (!a.ok()) {
        error = a.error;
        return false;
    }
    return true;
}

namespace {

struct Download {
    Program program;
    double lat, lon, radius;
    std::string line;
    std::string out;
    size_t kept = 0, done = 0;
    long total = -1;
    bool header = true, writeOk = true;
    void (*progress)(long, long);
    uint32_t nowYear;

    void flush() {
        if (!out.empty()) writeOk &= storage::appendFile("/HamOS/pota/near.tmp", out);
        out.clear();
    }

    void onLine(const std::string& l) {
        if (l.empty()) return;
        std::vector<std::string> f = csv::split(l);
        Place p;
        if (program == POTA) {
            // reference,name,active,entityId,locationDesc,latitude,longitude,grid
            if (f.size() < 7 || f[0] == "reference") return;
            if (f[2] == "0") return;                            // retired park
            p.ref = f[0];
            p.name = f[1];
            p.info = f[4];
            p.lat = atof(f[5].c_str());
            p.lon = atof(f[6].c_str());
        } else {
            // SummitCode,AssociationName,RegionName,SummitName,AltM,AltFt,GridRef1,GridRef2,Longitude,Latitude,Points,
            // BonusPoints,ValidFrom,ValidTo,...
            if (f.size() < 14 || f[0] == "SummitCode" || f[0].find('/') == std::string::npos) return;
            if (f[13].size() >= 10 && (uint32_t)atoi(f[13].c_str() + 6) < nowYear) return;     // withdrawn summit
            p.ref = f[0];
            p.name = f[3];
            p.info = f[4] + " m, " + f[10] + " pt";
            p.lon = atof(f[8].c_str());
            p.lat = atof(f[9].c_str());
        }
        if (p.lat == 0 && p.lon == 0) return;
        if (maidenhead::distanceKm(lat, lon, p.lat, p.lon) > radius) return;
        out += csv::field(p.ref) + "," + csv::field(p.name) + "," + csv::field(p.info) + "," + std::to_string(p.lat) + "," +
               std::to_string(p.lon) + "\n";
        kept++;
        if (out.size() > 4096) flush();
    }
};

}  // namespace

bool downloadNearby(Program p, double radiusKm, void (*progress)(long, long), size_t& kept, std::string& error) {
    Download d;
    if (!station::position(d.lat, d.lon)) {
        error = T("Set your grid locator or wait for GPS");
        return false;
    }
    if (!storage::ready()) {
        error = T("No SD card");
        return false;
    }
    d.program = p;
    d.radius = radiusKm;
    d.progress = progress;
    time_t now = net::timeValid() ? (time_t)(net::utcMillis() / 1000) : 0;
    struct tm t;
    gmtime_r(&now, &t);
    d.nowYear = now ? (uint32_t)(t.tm_year + 1900) : 0;
    storage::makeDir("/HamOS/pota");
    storage::remove("/HamOS/pota/near.tmp");
    http::Request r;
    r.url = p == POTA ? "https://pota.app/all_parks_ext.csv" : "https://storage.sota.org.uk/summitslist.csv";
    r.timeoutMs = 30000;
    http::Response a = http::stream(
            r,
            [](const uint8_t* data, size_t n, void* ctx) {
                Download* d = (Download*)ctx;
                for (size_t i = 0; i < n; i++) {
                    char c = (char)data[i];
                    if (c == '\n') {
                        d->onLine(d->line);
                        d->line.clear();
                    } else if (d->line.size() < 512) {
                        d->line += c;
                    }
                }
                d->done += n;
                if (d->progress) d->progress((long)d->done, d->total);
                return d->writeOk;
            },
            &d, [](long size, void* ctx) { ((Download*)ctx)->total = size; });
    d.onLine(d.line);
    d.flush();
    if (!a.ok() || !d.writeOk) {
        storage::remove("/HamOS/pota/near.tmp");
        error = !d.writeOk ? T("Could not write to the SD card") : a.error;
        return false;
    }
    storage::remove(nearbyPath(p));
    if (d.kept == 0) storage::writeFile(nearbyPath(p), "");
    else storage::rename("/HamOS/pota/near.tmp", nearbyPath(p));
    kept = d.kept;
    return true;
}

std::vector<Place> nearby(Program p, size_t max, std::string& error) {
    std::vector<Place> all;
    double lat, lon;
    if (!station::position(lat, lon)) {
        error = T("Set your grid locator or wait for GPS");
        return all;
    }
    if (!storage::exists(nearbyPath(p))) {
        error = T("Not downloaded yet: press D");
        return all;
    }
    struct Ctx {
        std::vector<Place>* all;
        double lat, lon;
        std::string line;
    } ctx{&all, lat, lon, ""};
    storage::readChunks(
            nearbyPath(p),
            [](const char* data, size_t n, void* u) {
                Ctx* c = (Ctx*)u;
                for (size_t i = 0; i < n; i++) {
                    if (data[i] != '\n') {
                        c->line += data[i];
                        continue;
                    }
                    std::vector<std::string> f = csv::split(c->line);
                    c->line.clear();
                    if (f.size() < 5) continue;
                    Place pl;
                    pl.ref = f[0];
                    pl.name = f[1];
                    pl.info = f[2];
                    pl.lat = atof(f[3].c_str());
                    pl.lon = atof(f[4].c_str());
                    pl.km = maidenhead::distanceKm(c->lat, c->lon, pl.lat, pl.lon);
                    pl.bearing = maidenhead::bearingDeg(c->lat, c->lon, pl.lat, pl.lon);
                    c->all->push_back(pl);
                    if (c->all->size() > 3000) {        // keep memory small: only the nearest matter
                        std::sort(c->all->begin(), c->all->end(), [](const Place& a, const Place& b) { return a.km < b.km; });
                        c->all->resize(500);
                    }
                }
            },
            &ctx);
    std::sort(all.begin(), all.end(), [](const Place& a, const Place& b) { return a.km < b.km; });
    if (all.size() > max) all.resize(max);
    return all;
}

int activationCount(Program p) {
    std::string ref = config::get(p == POTA ? "pota_ref" : "sota_ref");
    if (ref.empty() || !net::timeValid()) return 0;
    time_t now = (time_t)(net::utcMillis() / 1000);
    struct tm t;
    gmtime_r(&now, &t);
    char today[9];
    strftime(today, sizeof(today), "%Y%m%d", &t);
    int n = 0;
    for (const adif::Qso& q : logbook::recent())
        if (q.date == today && (p == POTA ? q.potaRef : q.sotaRef) == ref) n++;
    return n;
}

}  // namespace pota
