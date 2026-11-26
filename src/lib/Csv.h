// Splits one CSV line into fields ("quoted, with commas" and "" for a quote).
#pragma once
#include <string>
#include <vector>

namespace csv {

inline std::vector<std::string> split(const std::string& line, char sep = ',') {
    std::vector<std::string> out(1);
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { out.back() += '"'; i++; }
                else quoted = false;
            } else {
                out.back() += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == sep) {
            out.emplace_back();
        } else if (c != '\r' && c != '\n') {
            out.back() += c;
        }
    }
    return out;
}

// Quotes a field if it needs it.
inline std::string field(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

}  // namespace csv
