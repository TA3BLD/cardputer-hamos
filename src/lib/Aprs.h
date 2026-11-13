// APRS packet text format (TNC2 "SRC>DST,PATH:info"): positions, messages,
// and the APRS-IS login passcode. Pure C++, host-testable.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace aprs {

constexpr const char* kToCall = "APZHAM";   // APZ* = experimental software

struct Packet {
    std::string src, dst, path, info;
};

inline bool parseTnc2(const std::string& line, Packet& p) {
    size_t gt = line.find('>'), colon = line.find(':');
    if (gt == std::string::npos || colon == std::string::npos || gt == 0 || colon < gt + 2) return false;
    p.src = line.substr(0, gt);
    std::string route = line.substr(gt + 1, colon - gt - 1);
    size_t comma = route.find(',');
    p.dst = route.substr(0, comma);
    p.path = comma == std::string::npos ? "" : route.substr(comma + 1);
    p.info = line.substr(colon + 1);
    while (!p.info.empty() && (p.info.back() == '\r' || p.info.back() == '\n')) p.info.pop_back();
    return !p.info.empty();
}

struct Position {
    bool valid = false;
    double lat = 0, lon = 0;
    char symbolTable = '/', symbol = '[';
    std::string comment;
};

inline bool allDigits(const std::string& s, size_t from, size_t n) {
    for (size_t i = from; i < from + n; i++)
        if (i >= s.size() || !isdigit((unsigned char)s[i])) return false;
    return true;
}

// Position reports: '!' '=' (no timestamp), '/' '@' (7 character timestamp);
// plain "DDMM.hhN/DDDMM.hhE" and base-91 compressed forms. Mic-E is not decoded.
inline Position parsePosition(const std::string& info) {
    Position p;
    if (info.empty()) return p;
    size_t at;
    if (info[0] == '!' || info[0] == '=') at = 1;
    else if (info[0] == '/' || info[0] == '@') at = 8;
    else return p;
    if (info.size() < at + 13) return p;

    if (isdigit((unsigned char)info[at])) {
        if (info.size() < at + 19) return p;
        // DDMM.hhN s DDDMM.hhW c
        if (!allDigits(info, at, 4) || info[at + 4] != '.' || !allDigits(info, at + 9, 5) || info[at + 14] != '.') return p;
        double lat = atof(info.substr(at, 2).c_str()) + atof(info.substr(at + 2, 5).c_str()) / 60.0;
        double lon = atof(info.substr(at + 9, 3).c_str()) + atof(info.substr(at + 12, 5).c_str()) / 60.0;
        char ns = info[at + 7], ew = info[at + 17];
        if ((ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W') || lat > 90 || lon > 180) return p;
        p.lat = ns == 'S' ? -lat : lat;
        p.lon = ew == 'W' ? -lon : lon;
        p.symbolTable = info[at + 8];
        p.symbol = info[at + 18];
        p.comment = info.substr(at + 19);
    } else {
        // s YYYY XXXX c cs T   (base 91)
        long y = 0, x = 0;
        for (int i = 0; i < 4; i++) {
            int a = info[at + 1 + (size_t)i] - 33, b = info[at + 5 + (size_t)i] - 33;
            if (a < 0 || a > 90 || b < 0 || b > 90) return p;
            y = y * 91 + a;
            x = x * 91 + b;
        }
        p.lat = 90.0 - y / 380926.0;
        p.lon = -180.0 + x / 190463.0;
        if (p.lat < -90 || p.lat > 90 || p.lon < -180 || p.lon > 180) return p;
        p.symbolTable = info[at];
        p.symbol = info[at + 9];
        p.comment = info.substr(at + 13);
    }
    p.valid = true;
    return p;
}

struct Message {
    bool valid = false;
    std::string to, text, id;   // id: message number to acknowledge, may be empty
    bool isAck = false, isRej = false;
};

// ":ADDRESSEE:text{id"   and   ":ADDRESSEE:ackNN"
inline Message parseMessage(const std::string& info) {
    Message m;
    if (info.size() < 11 || info[0] != ':' || info[10] != ':') return m;
    m.to = info.substr(1, 9);
    while (!m.to.empty() && m.to.back() == ' ') m.to.pop_back();
    m.text = info.substr(11);
    if (m.text.compare(0, 3, "ack") == 0 && m.text.size() > 3 && m.text.size() <= 8) {
        m.isAck = true;
        m.id = m.text.substr(3);
    } else if (m.text.compare(0, 3, "rej") == 0 && m.text.size() > 3 && m.text.size() <= 8) {
        m.isRej = true;
        m.id = m.text.substr(3);
    } else {
        size_t brace = m.text.rfind('{');
        if (brace != std::string::npos && m.text.size() - brace <= 6) {
            m.id = m.text.substr(brace + 1);
            size_t close = m.id.find('}');      // reply-ack form "{AB}CD"
            if (close != std::string::npos) m.id.resize(close);
            m.text.resize(brace);
        }
    }
    m.valid = !m.to.empty();
    return m;
}

inline std::string header(const std::string& from, const std::string& path = "TCPIP*") {
    return from + ">" + kToCall + (path.empty() ? "" : "," + path) + ":";
}

inline std::string formatPosition(double lat, double lon, char symbolTable, char symbol, const std::string& comment) {
    char buf[32];
    double la = std::fabs(lat), lo = std::fabs(lon);
    int latDeg = (int)la, lonDeg = (int)lo;
    double latMin = (la - latDeg) * 60.0, lonMin = (lo - lonDeg) * 60.0;
    if (latMin >= 59.995) latMin = 59.99;       // never print 60.00 minutes
    if (lonMin >= 59.995) lonMin = 59.99;
    snprintf(buf, sizeof(buf), "!%02d%05.2f%c%c%03d%05.2f%c%c", latDeg, latMin, lat < 0 ? 'S' : 'N', symbolTable, lonDeg,
             lonMin, lon < 0 ? 'W' : 'E', symbol);
    return buf + comment;
}

inline std::string formatMessage(const std::string& to, const std::string& text, const std::string& id) {
    std::string addressee = to;
    addressee.resize(9, ' ');
    return ":" + addressee + ":" + text + (id.empty() ? "" : "{" + id);
}

inline std::string formatAck(const std::string& to, const std::string& id) { return formatMessage(to, "ack" + id, ""); }

// APRS-IS login passcode for a callsign (the SSID is ignored).
inline int passcode(const std::string& call) {
    std::string base = call.substr(0, call.find('-'));
    unsigned hash = 0x73e2;
    for (size_t i = 0; i < base.size(); i += 2) {
        hash ^= (unsigned)toupper((unsigned char)base[i]) << 8;
        if (i + 1 < base.size()) hash ^= (unsigned)toupper((unsigned char)base[i + 1]);
    }
    return (int)(hash & 0x7fff);
}

}  // namespace aprs
