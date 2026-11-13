// ADIF (Amateur Data Interchange Format) records. Pure C++, host-testable.
#pragma once
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace adif {

struct Qso {
    std::string call, freq, mode, rstSent, rstRcvd, date, time, comment;   // date YYYYMMDD, time HHMM (UTC)
};

// Amateur band name for a frequency in MHz, "" if outside the bands.
inline std::string band(double mhz) {
    struct B { double lo, hi; const char* name; };
    static const B bands[] = {
        {1.8, 2.0, "160m"}, {3.5, 4.0, "80m"}, {5.06, 5.45, "60m"}, {7.0, 7.3, "40m"},
        {10.1, 10.15, "30m"}, {14.0, 14.35, "20m"}, {18.068, 18.168, "17m"}, {21.0, 21.45, "15m"},
        {24.89, 24.99, "12m"}, {28.0, 29.7, "10m"}, {50.0, 54.0, "6m"}, {70.0, 71.0, "4m"},
        {144.0, 148.0, "2m"}, {420.0, 450.0, "70cm"}, {1240.0, 1300.0, "23cm"},
    };
    for (const B& b : bands)
        if (mhz >= b.lo && mhz <= b.hi) return b.name;
    return "";
}

inline std::string field(const char* name, const std::string& value) {
    if (value.empty()) return "";
    return std::string("<") + name + ":" + std::to_string(value.size()) + ">" + value + " ";
}

inline std::string header() {
    return "HamOS logbook\n" + field("ADIF_VER", "3.1.4") + field("PROGRAMID", "HamOS") + "<EOH>\n";
}

inline std::string toRecord(const Qso& q) {
    return field("CALL", q.call) + field("QSO_DATE", q.date) + field("TIME_ON", q.time) +
           field("FREQ", q.freq) + field("BAND", band(atof(q.freq.c_str()))) + field("MODE", q.mode) +
           field("RST_SENT", q.rstSent) + field("RST_RCVD", q.rstRcvd) + field("COMMENT", q.comment) +
           "<EOR>\n";
}

// Parses every record in an ADIF text. Unknown fields are ignored.
inline std::vector<Qso> parse(const std::string& text) {
    std::vector<Qso> out;
    Qso cur;
    bool any = false;
    size_t pos = 0;
    while ((pos = text.find('<', pos)) != std::string::npos) {
        size_t end = text.find('>', pos);
        if (end == std::string::npos) break;
        std::string tag = text.substr(pos + 1, end - pos - 1);
        pos = end + 1;
        for (char& c : tag) c = (char)toupper((unsigned char)c);
        if (tag == "EOH") {            // everything before it was header
            cur = Qso();
            any = false;
            continue;
        }
        if (tag == "EOR") {
            if (any && !cur.call.empty()) out.push_back(cur);
            cur = Qso();
            any = false;
            continue;
        }
        size_t colon = tag.find(':');
        if (colon == std::string::npos) continue;
        std::string name = tag.substr(0, colon);
        size_t len = (size_t)atoi(tag.c_str() + colon + 1);   // stops at an optional ":type"
        if (pos + len > text.size()) break;
        std::string value = text.substr(pos, len);
        pos += len;
        any = true;
        if (name == "CALL") cur.call = value;
        else if (name == "QSO_DATE") cur.date = value;
        else if (name == "TIME_ON") cur.time = value.substr(0, 4);
        else if (name == "FREQ") cur.freq = value;
        else if (name == "MODE") cur.mode = value;
        else if (name == "RST_SENT") cur.rstSent = value;
        else if (name == "RST_RCVD") cur.rstRcvd = value;
        else if (name == "COMMENT") cur.comment = value;
    }
    return out;
}

}  // namespace adif
