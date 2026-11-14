#include "Callbook.h"
#include <map>
#include "Config.h"
#include "Http.h"
#include "Lang.h"
#include "lib/MiniXml.h"

namespace callbook {

namespace {

std::string session;
std::map<std::string, Info> cache;

bool useHamqth() { return !config::get("hamqth_user").empty(); }
bool useQrz() { return !useHamqth() && !config::get("qrz_user").empty(); }

bool login(std::string& error) {
    http::Request r;
    if (useHamqth()) {
        r.url = "https://www.hamqth.com/xml.php?u=" + http::urlEncode(config::get("hamqth_user")) +
                "&p=" + http::urlEncode(config::get("hamqth_pass"));
    } else {
        r.url = "https://xmldata.qrz.com/xml/current/?username=" + http::urlEncode(config::get("qrz_user")) +
                ";password=" + http::urlEncode(config::get("qrz_pass")) + ";agent=HamOS";
    }
    http::Response a = http::fetch(r);
    if (!a.ok()) {
        error = a.error;
        return false;
    }
    session = useHamqth() ? xml::text(a.body, "session_id") : xml::text(a.body, "Key");
    if (session.empty()) {
        error = xml::text(a.body, "error");
        if (error.empty()) error = T("Callbook login failed");
        return false;
    }
    return true;
}

bool ask(const std::string& call, Info& out, std::string& error, bool& sessionGone) {
    sessionGone = false;
    http::Request r;
    r.url = useHamqth() ? "https://www.hamqth.com/xml.php?id=" + session + "&callsign=" + http::urlEncode(call) + "&prg=HamOS"
                        : "https://xmldata.qrz.com/xml/current/?s=" + session + ";callsign=" + http::urlEncode(call);
    http::Response a = http::fetch(r);
    if (!a.ok()) {
        error = a.error;
        return false;
    }
    std::string err = xml::text(a.body, "error");
    if (!err.empty()) {
        std::string low = err;
        for (char& c : low) c = (char)tolower((unsigned char)c);
        sessionGone = low.find("session") != std::string::npos || low.find("expired") != std::string::npos ||
                      low.find("invalid") != std::string::npos;
        error = low.find("not found") != std::string::npos ? T("Not in the callbook") : err;
        return false;
    }
    out = Info();
    if (useHamqth()) {
        out.call = xml::text(a.body, "callsign");
        out.name = xml::text(a.body, "nick");
        if (out.name.empty()) out.name = xml::text(a.body, "adr_name");
        out.qth = xml::text(a.body, "qth");
        if (out.qth.empty()) out.qth = xml::text(a.body, "adr_city");
        out.grid = xml::text(a.body, "grid");
        out.country = xml::text(a.body, "country");
    } else {
        out.call = xml::text(a.body, "call");
        out.name = xml::text(a.body, "fname");
        out.qth = xml::text(a.body, "addr2");
        out.grid = xml::text(a.body, "grid");
        out.country = xml::text(a.body, "country");
    }
    for (char& c : out.call) c = (char)toupper((unsigned char)c);
    if (out.grid.size() >= 4) {
        out.grid[0] = (char)toupper((unsigned char)out.grid[0]);
        out.grid[1] = (char)toupper((unsigned char)out.grid[1]);
        for (size_t i = 4; i < out.grid.size(); i++) out.grid[i] = (char)tolower((unsigned char)out.grid[i]);
    }
    if (out.call.empty()) out.call = call;
    return true;
}

}  // namespace

bool configured() { return useHamqth() || useQrz(); }
const char* service() { return useHamqth() ? "HamQTH" : useQrz() ? "QRZ.com" : ""; }

bool lookup(const std::string& callIn, Info& out, std::string& error) {
    std::string call = callIn;
    for (char& c : call) c = (char)toupper((unsigned char)c);
    auto it = cache.find(call);
    if (it != cache.end()) {
        out = it->second;
        return true;
    }
    if (!configured()) {
        error = T("Set a HamQTH or QRZ user in Settings");
        return false;
    }
    if (session.empty() && !login(error)) return false;
    bool gone = false;
    bool ok = ask(call, out, error, gone);
    if (!ok && gone) {                                  // session expired: log in again once
        session.clear();
        ok = login(error) && ask(call, out, error, gone);
    }
    if (!ok) return false;
    if (cache.size() > 40) cache.erase(cache.begin());
    cache[call] = out;
    error.clear();
    return true;
}

}  // namespace callbook
