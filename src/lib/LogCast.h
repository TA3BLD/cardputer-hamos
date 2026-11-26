// Contacts for PC logging programs, as they are logged:
//   N1MM Logger+ "contactinfo" XML (Log4OM, DXKeeper's gateway, N1MM, many
//   others listen for it; usual port 12060)
//   or a plain ADIF record (GridTracker, Log4OM "ADIF message", port 2333).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include "Adif.h"
#include "Dxcc.h"

namespace logcast {

inline std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

inline std::string n1mm(const adif::Qso& q, const std::string& myCall) {
    double mhz = atof(q.freq.c_str());
    char freq[16], band[16];
    snprintf(freq, sizeof(freq), "%ld", (long)(mhz * 1e5 + 0.5));      // N1MM counts in 10 Hz
    // N1MM's band field: the band's lower edge in MHz (1.8, 3.5, 7, 14, 50, 144 ...)
    static const double edges[] = {1.8, 3.5, 5.3, 7, 10.1, 14, 18, 21, 24, 28, 50, 70, 144, 222, 420, 1240};
    double edge = 0;
    for (double x : edges)
        if (mhz >= x) edge = x;
    snprintf(band, sizeof(band), "%g", edge);
    std::string ts = q.date.size() == 8 ? q.date.substr(0, 4) + "-" + q.date.substr(4, 2) + "-" + q.date.substr(6, 2) : "";
    ts += " " + (q.time.size() >= 4 ? q.time.substr(0, 2) + ":" + q.time.substr(2, 2) + ":00" : std::string("00:00:00"));
    const dxcc::Entity* e = dxcc::lookup(q.call);
    auto tag = [](const char* name, const std::string& value) { return std::string("<") + name + ">" + xmlEscape(value) + "</" + name + ">"; };
    return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<contactinfo>") + tag("app", "HamOS") + tag("contestname", "DX") +
           tag("contestnr", "1") + tag("timestamp", ts) + tag("mycall", myCall) + tag("band", band) + tag("rxfreq", freq) +
           tag("txfreq", freq) + tag("operator", myCall) + tag("mode", q.mode) + tag("call", q.call) +
           tag("countryprefix", "") + tag("continent", e && e->continent ? e->continent : "") + tag("snt", q.rstSent) +
           tag("sntnr", q.stx) + tag("rcv", q.rstRcvd) + tag("rcvnr", q.srx) + tag("gridsquare", q.grid) + tag("exchange1", "") +
           tag("comment", q.comment) + tag("qth", q.qth) + tag("name", q.name) + tag("points", "1") + tag("radionr", "1") +
           tag("IsOriginal", "True") + tag("NetBiosName", "HamOS") + tag("StationName", "HamOS") + tag("IsRunQSO", "0") +
           tag("ID", q.call + q.date + q.time) + "</contactinfo>";
}

inline std::string adifRecord(const adif::Qso& q) { return adif::toRecord(q); }

}  // namespace logcast
