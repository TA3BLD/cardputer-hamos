// Cabrillo 3.0 contest logs from logbook contacts.
//
//   QSO: 14025 CW 2026-11-14 1203 TA3BLD        599 001    DL1ABC        599 023
//
// The exchange is RST + serial number (STX / SRX) when the contacts have them,
// otherwise RST only.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include "Adif.h"

namespace cabrillo {

struct Header {
    std::string contest, callsign, category = "SINGLE-OP ALL LOW", location, name, email, operators, soapbox;
};

inline std::string header(const Header& h) {
    std::string s = "START-OF-LOG: 3.0\nCREATED-BY: HamOS\n";
    s += "CONTEST: " + h.contest + "\n";
    s += "CALLSIGN: " + h.callsign + "\n";
    size_t a = 0;
    std::string cat = h.category;
    // "SINGLE-OP ALL LOW" -> the three standard lines
    std::string parts[3];
    for (int i = 0; i < 3; i++) {
        size_t b = cat.find(' ', a);
        parts[i] = cat.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    if (!parts[0].empty()) s += "CATEGORY-OPERATOR: " + parts[0] + "\n";
    if (!parts[1].empty()) s += "CATEGORY-BAND: " + parts[1] + "\n";
    if (!parts[2].empty()) s += "CATEGORY-POWER: " + parts[2] + "\n";
    if (!h.location.empty()) s += "LOCATION: " + h.location + "\n";
    if (!h.name.empty()) s += "NAME: " + h.name + "\n";
    if (!h.email.empty()) s += "EMAIL: " + h.email + "\n";
    s += "OPERATORS: " + (h.operators.empty() ? h.callsign : h.operators) + "\n";
    if (!h.soapbox.empty()) s += "SOAPBOX: " + h.soapbox + "\n";
    return s;
}

// Cabrillo mode: CW, PH (voice), FM, RY (RTTY), DG (other digital).
inline const char* mode(const std::string& adifMode) {
    if (adifMode == "CW") return "CW";
    if (adifMode == "SSB" || adifMode == "USB" || adifMode == "LSB" || adifMode == "AM") return "PH";
    if (adifMode == "FM") return "FM";
    if (adifMode == "RTTY") return "RY";
    return "DG";
}

// Frequency in kHz for HF, the band for 50 MHz and up (as the rules ask).
inline std::string freq(const std::string& mhzText) {
    double mhz = atof(mhzText.c_str());
    if (mhz <= 0) return "0";
    if (mhz < 30) {
        char b[16];
        snprintf(b, sizeof(b), "%d", (int)(mhz * 1000 + 0.5));
        return b;
    }
    if (mhz < 54) return "50";
    if (mhz < 72) return "70";
    if (mhz < 149) return "144";
    if (mhz < 226) return "222";
    if (mhz < 451) return "432";
    if (mhz < 1300) return "1.2G";
    return "2.3G";
}

inline std::string pad(const std::string& s, size_t w) { return s.size() >= w ? s : s + std::string(w - s.size(), ' '); }

inline std::string qsoLine(const adif::Qso& q, const std::string& myCall) {
    std::string date = q.date.size() == 8 ? q.date.substr(0, 4) + "-" + q.date.substr(4, 2) + "-" + q.date.substr(6, 2) : "0000-00-00";
    std::string time = q.time.size() >= 4 ? q.time.substr(0, 4) : "0000";
    std::string rs = q.rstSent.empty() ? (q.mode == "CW" || q.mode == "RTTY" ? "599" : "59") : q.rstSent;
    std::string rr = q.rstRcvd.empty() ? rs : q.rstRcvd;
    auto serial = [](const std::string& n) {
        if (n.empty()) return std::string();
        char b[16];
        snprintf(b, sizeof(b), "%03d", atoi(n.c_str()));
        return std::string(b);
    };
    std::string line = "QSO: " + pad(freq(q.freq), 5) + " " + mode(q.mode) + " " + date + " " + time + " " + pad(myCall, 13) + " " +
                       pad(rs, 3) + " " + pad(serial(q.stx), 6) + " " + pad(q.call, 13) + " " + pad(rr, 3) + " " + serial(q.srx);
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return line + "\n";
}

inline std::string footer() { return "END-OF-LOG:\n"; }

}  // namespace cabrillo
