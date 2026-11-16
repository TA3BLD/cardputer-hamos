// Watch list patterns: "3Y0J" (a call), "VK9*" (a prefix), "TA-0042" or
// "TA-*" (a POTA/SOTA reference), "NEW" (a country not in the log yet).
#pragma once
#include <string>
#include <vector>

namespace watchlist {

inline std::vector<std::string> parse(const std::string& list) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : list + ",") {
        if (c == ',' || c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += (char)toupper((unsigned char)c);
        }
    }
    return out;
}

inline bool match(const std::string& pattern, const std::string& value) {
    if (value.empty()) return false;
    std::string v;
    for (char c : value) v += (char)toupper((unsigned char)c);
    if (!pattern.empty() && pattern.back() == '*') return v.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    if (v == pattern) return true;
    // a call with a suffix or prefix: "3Y0J/P", "DL/3Y0J"
    size_t p = v.find(pattern);
    if (p == std::string::npos) return false;
    bool before = p == 0 || v[p - 1] == '/';
    bool after = p + pattern.size() == v.size() || v[p + pattern.size()] == '/';
    return before && after;
}

}  // namespace watchlist
