// NMEA 0183 parser for GNSS receivers (GGA and RMC sentences, any talker).
// Pure C++, host-testable.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace nmea {

struct Fix {
    bool valid = false;             // position is usable
    double lat = 0, lon = 0;        // degrees, north/east positive
    double altitudeM = 0, speedKmh = 0, courseDeg = 0;
    int satellites = 0;
    bool timeValid = false;         // date and time below are set
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

class Parser {
public:
    Fix fix;
    uint32_t sentences = 0;         // valid sentences seen (shows that a receiver is attached)

    // Feed received characters one at a time. Returns true when a sentence updated `fix`.
    bool feed(char c) {
        if (c == '$') { line_ = "$"; return false; }
        if (c == '\r' || c == '\n') {
            bool updated = !line_.empty() && parse(line_);
            line_.clear();
            return updated;
        }
        if (!line_.empty() && line_.size() < 120) line_ += c;
        return false;
    }

    bool parse(const std::string& s) {
        size_t star = s.find('*');
        if (s.size() < 9 || s[0] != '$' || star == std::string::npos || star + 3 > s.size()) return false;
        uint8_t sum = 0;
        for (size_t i = 1; i < star; i++) sum ^= (uint8_t)s[i];
        if (sum != (uint8_t)strtol(s.substr(star + 1, 2).c_str(), nullptr, 16)) return false;

        std::vector<std::string> f;
        size_t start = 1;
        while (true) {
            size_t comma = s.find(',', start);
            if (comma == std::string::npos || comma > star) { f.push_back(s.substr(start, star - start)); break; }
            f.push_back(s.substr(start, comma - start));
            start = comma + 1;
        }
        if (f[0].size() != 5) return false;
        sentences++;
        std::string type = f[0].substr(2);
        if (type == "GGA" && f.size() >= 10) {
            int quality = atoi(f[6].c_str());
            fix.satellites = atoi(f[7].c_str());
            if (quality > 0 && position(f[2], f[3], f[4], f[5])) {
                fix.altitudeM = atof(f[9].c_str());
                fix.valid = true;
            } else {
                fix.valid = false;
            }
            return true;
        }
        if (type == "RMC" && f.size() >= 10) {
            bool active = f[2] == "A";
            if (active && position(f[3], f[4], f[5], f[6])) {
                fix.speedKmh = atof(f[7].c_str()) * 1.852;
                fix.courseDeg = atof(f[8].c_str());
                fix.valid = true;
            } else if (!active) {
                fix.valid = false;
            }
            if (f[1].size() >= 6 && f[9].size() == 6) {
                fix.hour = two(f[1], 0); fix.minute = two(f[1], 2); fix.second = two(f[1], 4);
                fix.day = two(f[9], 0); fix.month = two(f[9], 2); fix.year = 2000 + two(f[9], 4);
                // Receivers report a default date (often 2080 or 1980) before they have real time.
                fix.timeValid = fix.year >= 2024 && fix.year < 2080 && fix.month >= 1 && fix.month <= 12 &&
                                fix.day >= 1 && fix.day <= 31;
            }
            return true;
        }
        return false;
    }

private:
    static int two(const std::string& s, size_t at) { return (s[at] - '0') * 10 + (s[at + 1] - '0'); }

    // "ddmm.mmmm","N" / "dddmm.mmmm","E"
    bool position(const std::string& lat, const std::string& ns, const std::string& lon, const std::string& ew) {
        if (lat.size() < 4 || lon.size() < 5 || ns.empty() || ew.empty()) return false;
        double la = atof(lat.substr(0, 2).c_str()) + atof(lat.substr(2).c_str()) / 60.0;
        double lo = atof(lon.substr(0, 3).c_str()) + atof(lon.substr(3).c_str()) / 60.0;
        if (la > 90 || lo > 180) return false;
        fix.lat = ns == "S" ? -la : la;
        fix.lon = ew == "W" ? -lo : lo;
        return true;
    }

    std::string line_;
};

}  // namespace nmea
