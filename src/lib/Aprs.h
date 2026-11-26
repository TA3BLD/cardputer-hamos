// APRS packet text format (TNC2 "SRC>DST,PATH:info"): positions, messages,
// and the APRS-IS login passcode. Pure C++, host-testable.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

// Mic-E: the compact format of most APRS radios (Kenwood, Yaesu) and many
// trackers. Latitude and some flags are hidden in the destination "callsign",
// longitude, speed and course in the first bytes of the information field.
inline Position parseMicE(const std::string& dst, const std::string& info) {
    Position p;
    if (info.size() < 9 || (info[0] != '`' && info[0] != '\'' && info[0] != 0x1c && info[0] != 0x1d)) return p;
    std::string d = dst.substr(0, dst.find('-'));
    if (d.size() != 6) return p;
    int digits[6];
    bool south = false, plus100 = false, west = false;
    for (int i = 0; i < 6; i++) {
        char c = d[(size_t)i];
        bool high;                                  // 'P'-'Z': north / +100 / west
        if (c >= '0' && c <= '9') { digits[i] = c - '0'; high = false; }
        else if (c >= 'A' && c <= 'J') { digits[i] = c - 'A'; high = false; }
        else if (c == 'K' || c == 'L') { digits[i] = 0; high = false; }
        else if (c >= 'P' && c <= 'Y') { digits[i] = c - 'P'; high = true; }
        else if (c == 'Z') { digits[i] = 0; high = true; }
        else return p;
        if (i == 3) south = !high;
        if (i == 4) plus100 = high;
        if (i == 5) west = high;
    }
    double lat = digits[0] * 10 + digits[1] + (digits[2] * 10 + digits[3] + (digits[4] * 10 + digits[5]) / 100.0) / 60.0;
    int deg = (unsigned char)info[1] - 28 + (plus100 ? 100 : 0);
    if (deg >= 180 && deg <= 189) deg -= 80;
    else if (deg >= 190 && deg <= 199) deg -= 190;
    int min = (unsigned char)info[2] - 28;
    if (min >= 60) min -= 60;
    int hundredths = (unsigned char)info[3] - 28;
    if (deg < 0 || deg > 179 || min < 0 || min > 59 || hundredths < 0 || hundredths > 99 || lat > 90) return p;
    double lon = deg + (min + hundredths / 100.0) / 60.0;
    p.lat = south ? -lat : lat;
    p.lon = west ? -lon : lon;
    p.symbol = info[7];
    p.symbolTable = info[8];
    p.comment = info.substr(9);
    // An altitude "xyz}" (base 91) may open the comment, after an optional type byte.
    for (size_t at : {(size_t)0, (size_t)1})
        if (p.comment.size() >= at + 4 && p.comment[at + 3] == '}') { p.comment.erase(0, at + 4); break; }
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

// Digipeating by the WIDEn-N rules. Returns the packet to send again, or ""
// when this station should not repeat it. Fill-in digi (WIDE1-1) always;
// WIDE2-n too with `wide`. Our own callsign in the path is always served.
//   SRC>APRS,WIDE1-1,WIDE2-1:... -> SRC>APRS,MYCALL,WIDE1*,WIDE2-1:...
//   SRC>APRS,WIDE2-2:...         -> SRC>APRS,MYCALL*,WIDE2-1:...  (wide)
inline std::string digipeat(const std::string& tnc2, const std::string& myCall, bool wide) {
    Packet p;
    if (!parseTnc2(tnc2, p) || p.src == myCall || myCall.empty()) return "";
    std::vector<std::string> path;
    size_t a = 0;
    while (a <= p.path.size() && !p.path.empty()) {
        size_t b = p.path.find(',', a);
        path.push_back(p.path.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    if (path.size() > 7) return "";
    int used = -1;                                  // last element already digipeated
    for (size_t i = 0; i < path.size(); i++) {
        if (!path[i].empty() && path[i].back() == '*') {
            used = (int)i;
            path[i].pop_back();
        }
        if (path[i] == myCall && (int)i <= used) return "";      // we already did
    }
    size_t next = (size_t)(used + 1);
    if (next >= path.size()) return "";
    std::string el = path[next];
    std::vector<std::string> out(path.begin(), path.begin() + (long)next);
    int mark = -1;                                  // index in `out` that gets the '*'
    if (el == myCall) {
        out.push_back(myCall);
        mark = (int)out.size() - 1;
    } else if (el == "WIDE1-1") {
        out.push_back(myCall);
        out.push_back("WIDE1");
        mark = (int)out.size() - 1;
    } else if (wide && el.size() == 7 && el.compare(0, 5, "WIDE2") == 0 && el[5] == '-' && el[6] >= '1' && el[6] <= '2') {
        int n = el[6] - '0';
        out.push_back(myCall);
        if (n == 1) {
            out.push_back("WIDE2");
            mark = (int)out.size() - 1;
        } else {
            mark = (int)out.size() - 1;
            out.push_back("WIDE2-" + std::to_string(n - 1));
        }
    } else {
        return "";
    }
    for (size_t i = next + 1; i < path.size(); i++) out.push_back(path[i]);
    std::string route = p.dst;
    for (size_t i = 0; i < out.size(); i++) route += "," + out[i] + ((int)i == mark ? "*" : "");
    return p.src + ">" + route + ":" + p.info;
}

// Position report with weather (APRS 1.01 chapter 12), symbol '_'.
//   tempC: temperature; humidity %; hpa: sea-level pressure; NAN when unknown
inline std::string formatWeather(double lat, double lon, double tempC, double humidity, double hpa, const std::string& comment) {
    std::string wx = ".../...g...";                 // no wind sensor
    char b[16];
    if (tempC == tempC) {
        int f = (int)std::lround(tempC * 9.0 / 5.0 + 32.0);
        snprintf(b, sizeof(b), "t%03d", f);       // -5 F prints as "t-05"
        wx += b;
    } else {
        wx += "t...";
    }
    if (humidity == humidity) {
        int h = (int)std::lround(humidity);
        snprintf(b, sizeof(b), "h%02d", h >= 100 ? 0 : h < 1 ? 1 : h);
        wx += b;
    }
    if (hpa == hpa) {
        snprintf(b, sizeof(b), "b%05d", (int)std::lround(hpa * 10));
        wx += b;
    }
    return formatPosition(lat, lon, '/', '_', wx + comment);
}

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
