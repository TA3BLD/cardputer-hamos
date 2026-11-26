#pragma once
#include <map>
#include "Arduino.h"
// Non-volatile key/value storage, kept in memory for the run.
inline std::map<std::string, std::map<std::string, std::string>> sim_nvs;
struct Preferences {
    std::string ns;
    bool begin(const char* name, bool = false) { ns = name; return true; }
    void end() {}
    bool getBool(const char* k, bool def = false) { auto& m = sim_nvs[ns]; return m.count(k) ? m[k] == "1" : def; }
    size_t putBool(const char* k, bool v) { sim_nvs[ns][k] = v ? "1" : "0"; return 1; }
    uint32_t getUInt(const char* k, uint32_t def = 0) { auto& m = sim_nvs[ns]; return m.count(k) ? (uint32_t)strtoul(m[k].c_str(), nullptr, 10) : def; }
    size_t putUInt(const char* k, uint32_t v) { sim_nvs[ns][k] = std::to_string(v); return 4; }
    String getString(const char* k, const String& def = String()) { auto& m = sim_nvs[ns]; return m.count(k) ? String(m[k]) : def; }
    size_t putString(const char* k, const String& v) { sim_nvs[ns][k] = v.s; return v.s.size(); }
    bool remove(const char* k) { return sim_nvs[ns].erase(k) > 0; }
};
