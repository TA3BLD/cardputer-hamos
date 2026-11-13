// Parser for DX cluster spot lines. Pure C++, host-testable.
//   "DX de EA5XX:     14025.0  JA1ABC       CW 599 up 1                1234Z"
#pragma once
#include <cstdlib>
#include <string>

namespace dxspot {

struct Spot {
    std::string spotter, call, comment, time;   // time "1234Z"
    double khz = 0;
};

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n\a");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n\a") - a + 1);
}

inline bool parse(const std::string& line, Spot& out) {
    if (line.compare(0, 6, "DX de ") != 0) return false;
    size_t colon = line.find(':', 6);
    if (colon == std::string::npos) return false;
    out = Spot();
    out.spotter = trim(line.substr(6, colon - 6));

    size_t p = line.find_first_not_of(' ', colon + 1);
    if (p == std::string::npos) return false;
    size_t e = line.find(' ', p);
    if (e == std::string::npos) return false;
    out.khz = atof(line.substr(p, e - p).c_str());

    p = line.find_first_not_of(' ', e);
    if (p == std::string::npos) return false;
    e = line.find(' ', p);
    out.call = line.substr(p, e == std::string::npos ? std::string::npos : e - p);

    std::string rest = e == std::string::npos ? "" : trim(line.substr(e));
    // The time, when present, is the last word: four digits and 'Z'.
    if (rest.size() >= 5 && rest.back() == 'Z' && rest.find_first_not_of("0123456789", rest.size() - 5) == rest.size() - 1) {
        out.time = rest.substr(rest.size() - 5);
        rest = trim(rest.substr(0, rest.size() - 5));
    }
    out.comment = rest;
    return out.khz > 0 && !out.call.empty() && !out.spotter.empty();
}

}  // namespace dxspot
