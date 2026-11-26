// Parser for the solar/propagation feed of hamqsl.com (solarxml.php).
// Pure C++, host-testable.
#pragma once
#include <string>

namespace solar {

struct Data {
    bool valid = false;
    std::string flux, sunspots, aIndex, kIndex, xray, updated;
    // Rows: 80m-40m, 30m-20m, 17m-15m, 12m-10m. Columns: day, night. "Good" / "Fair" / "Poor".
    std::string cond[4][2];
};

inline const char* bandName(int row) {
    static const char* names[4] = {"80m-40m", "30m-20m", "17m-15m", "12m-10m"};
    return names[row];
}

inline std::string tag(const std::string& xml, const char* name) {
    std::string open = std::string("<") + name + ">";
    size_t a = xml.find(open);
    if (a == std::string::npos) return "";
    a += open.size();
    size_t b = xml.find("</", a);
    if (b == std::string::npos) return "";
    std::string v = xml.substr(a, b - a);
    size_t s = v.find_first_not_of(" \t\r\n"), e = v.find_last_not_of(" \t\r\n");
    return s == std::string::npos ? "" : v.substr(s, e - s + 1);
}

inline Data parse(const std::string& xml) {
    Data d;
    d.flux = tag(xml, "solarflux");
    d.sunspots = tag(xml, "sunspots");
    d.aIndex = tag(xml, "aindex");
    d.kIndex = tag(xml, "kindex");
    d.xray = tag(xml, "xray");
    d.updated = tag(xml, "updated");
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 2; col++) {
            std::string open = std::string("<band name=\"") + bandName(row) + "\" time=\"" + (col ? "night" : "day") + "\">";
            size_t a = xml.find(open);
            if (a == std::string::npos) continue;
            a += open.size();
            size_t b = xml.find('<', a);
            if (b != std::string::npos) d.cond[row][col] = xml.substr(a, b - a);
        }
    d.valid = !d.flux.empty() && !d.kIndex.empty();
    return d;
}

}  // namespace solar
