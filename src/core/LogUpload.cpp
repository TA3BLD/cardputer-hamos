#include "LogUpload.h"
#include <vector>
#include "Config.h"
#include "Http.h"
#include "Lang.h"
#include "Log.h"
#include "Net.h"
#include "Storage.h"
#include "lib/Cabrillo.h"

namespace logupload {

namespace {

const char* kMarker[COUNT] = {"up_eqsl", "up_clublog", "up_qrz"};

long long marker(Service s) { return atoll(config::get(kMarker[s], "0").c_str()); }

void setMarker(Service s, long long t) {
    config::set(kMarker[s], std::to_string(t));
    config::save();
}

enum Outcome { SENT, DUPE, FAILED, STOP };

// One contact to one service. STOP: no point in trying the rest (login, network).
Outcome send(Service s, const adif::Qso& q, std::string& error) {
    std::string record = adif::toRecord(q);
    http::Request r;
    http::Response a;
    if (s == EQSL) {
        std::string data = "HamOS upload <ADIF_VER:5>3.1.4 <EOH> " + record;
        r.url = "https://www.eqsl.cc/qslcard/importADIF.cfm?ADIFData=" + http::urlEncode(data) +
                "&EQSL_USER=" + http::urlEncode(config::get("eqsl_user")) + "&EQSL_PSWD=" + http::urlEncode(config::get("eqsl_pass"));
        a = http::fetch(r);
        if (!a.ok()) { error = a.error; return STOP; }
        if (a.body.find("Duplicate") != std::string::npos) return DUPE;
        if (a.body.find("Result: 1 out of 1") != std::string::npos) return SENT;
        if (a.body.find("No match on eQSL_User") != std::string::npos || a.body.find("Password") != std::string::npos) {
            error = T("eQSL login failed");
            return STOP;
        }
        size_t e = a.body.find("Error:");
        error = e != std::string::npos ? a.body.substr(e, a.body.find_first_of("<\r\n", e) - e) : T("eQSL refused the contact");
        return FAILED;
    }
    if (s == CLUBLOG) {
        r.method = "POST";
        r.url = "https://clublog.org/realtime.php";
        r.contentType = "application/x-www-form-urlencoded";
        r.body = "email=" + http::urlEncode(config::get("clublog_email")) + "&password=" + http::urlEncode(config::get("clublog_pass")) +
                 "&callsign=" + http::urlEncode(config::get("callsign")) + "&api=" + http::urlEncode(config::get("clublog_key")) +
                 "&adif=" + http::urlEncode(record);
        a = http::fetch(r);
        if (a.status == 403) { error = T("Club Log login failed"); return STOP; }
        if (a.status == 0) { error = a.error; return STOP; }
        if (a.body.find("Dupe") != std::string::npos) return DUPE;
        if (a.ok()) return SENT;
        error = a.body.substr(0, 60);
        return FAILED;
    }
    r.method = "POST";      // QRZ logbook
    r.url = "https://logbook.qrz.com/api";
    r.contentType = "application/x-www-form-urlencoded";
    r.body = "KEY=" + http::urlEncode(config::get("qrz_logkey")) + "&ACTION=INSERT&ADIF=" + http::urlEncode(record);
    a = http::fetch(r);
    if (!a.ok()) { error = a.error; return STOP; }
    if (a.body.find("RESULT=OK") != std::string::npos || a.body.find("RESULT=REPLACE") != std::string::npos) return SENT;
    if (a.body.find("RESULT=AUTH") != std::string::npos) { error = T("QRZ logbook key not accepted"); return STOP; }
    if (a.body.find("uplicate") != std::string::npos) return DUPE;
    size_t e = a.body.find("REASON=");
    error = e != std::string::npos ? a.body.substr(e + 7, 60) : T("QRZ refused the contact");
    return FAILED;
}

}  // namespace

const char* name(Service s) { return s == EQSL ? "eQSL" : s == CLUBLOG ? "Club Log" : "QRZ.com"; }

const char* missingSetting(Service s) {
    if (s == EQSL) return config::get("eqsl_user").empty() ? "eQSL user" : config::get("eqsl_pass").empty() ? "eQSL password" : "";
    if (s == CLUBLOG) {
        if (config::get("clublog_email").empty()) return "Club Log email";
        if (config::get("clublog_pass").empty()) return "Club Log password";
        if (config::get("clublog_key").empty()) return "Club Log API key";
        return config::get("callsign").empty() ? "Callsign" : "";
    }
    return config::get("qrz_logkey").empty() ? "QRZ logbook key" : "";
}

bool configured(Service s) { return !*missingSetting(s); }

Counts count() {
    Counts c;
    long long marks[COUNT];
    for (int i = 0; i < COUNT; i++) marks[i] = marker((Service)i);
    struct Ctx {
        Counts* c;
        long long* marks;
    } ctx{&c, marks};
    logbook::forEach(
            [](const adif::Qso& q, void* u) {
                Ctx* x = (Ctx*)u;
                long long t = adif::startUnix(q);
                x->c->total++;
                for (int i = 0; i < COUNT; i++)
                    if (t > x->marks[i]) x->c->waiting[i]++;
            },
            &ctx);
    return c;
}

Result upload(Service s, bool all, void (*progress)(int, int)) {
    Result res;
    if (!configured(s)) {
        res.error = std::string(T("Missing setting")) + ": " + missingSetting(s);
        return res;
    }
    if (!net::connected()) {
        res.error = T("No WiFi");
        return res;
    }
    // Collect the contacts to send first (newer than the marker), oldest first.
    struct Ctx {
        long long since;
        std::vector<adif::Qso> list;
    } ctx{all ? 0 : marker(s), {}};
    logbook::forEach(
            [](const adif::Qso& q, void* u) {
                Ctx* c = (Ctx*)u;
                if (adif::startUnix(q) > c->since && c->list.size() < 2000) c->list.push_back(q);
            },
            &ctx);
    long long newest = marker(s);
    int n = (int)ctx.list.size();
    for (int i = 0; i < n; i++) {
        const adif::Qso& q = ctx.list[i];
        std::string error;
        Outcome o = send(s, q, error);
        if (o == STOP) {
            res.error = error;
            break;
        }
        if (o == SENT) res.sent++;
        else if (o == DUPE) res.dupes++;
        else {
            res.failed++;
            res.error = error;
        }
        long long t = adif::startUnix(q);
        if (t > newest) newest = t;
        if (progress) progress(i + 1, n);
    }
    if (newest > marker(s)) setMarker(s, newest);
    return res;
}

bool exportCabrillo(int hours, size_t& contacts, std::string& error) {
    std::string call = config::get("callsign");
    if (call.empty()) {
        error = T("Set your callsign in Settings");
        return false;
    }
    long long since = 0;
    if (hours > 0 && net::timeValid()) since = net::utcMillis() / 1000 - (long long)hours * 3600;
    cabrillo::Header h;
    h.callsign = call;
    h.contest = config::get("contest_name", "DX");
    h.category = config::get("contest_category", "SINGLE-OP ALL LOW");
    h.name = config::get("op_name");
    struct Ctx {
        long long since;
        std::string call, out;
        size_t n = 0;
        bool ok = true;
    } ctx{since, call, cabrillo::header(h)};
    storage::remove(kCabrilloPath);
    logbook::forEach(
            [](const adif::Qso& q, void* u) {
                Ctx* c = (Ctx*)u;
                if (adif::startUnix(q) < c->since) return;
                c->out += cabrillo::qsoLine(q, c->call);
                c->n++;
                if (c->out.size() > 2048) {
                    c->ok &= storage::appendFile(kCabrilloPath, c->out);
                    c->out.clear();
                }
            },
            &ctx);
    ctx.out += cabrillo::footer();
    ctx.ok &= storage::appendFile(kCabrilloPath, ctx.out);
    contacts = ctx.n;
    if (!ctx.ok) error = T("Could not write to the SD card");
    return ctx.ok;
}

}  // namespace logupload
