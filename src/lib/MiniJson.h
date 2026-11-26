// Just enough JSON for the USB link: requests are flat objects (string,
// number, true/false/null values); replies are built with Writer.
// Pure C++, host-testable.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace json {

// A flat object: every value kept as text (strings unescaped).
struct Object {
    std::map<std::string, std::string> values;
    bool has(const std::string& k) const { return values.count(k) > 0; }
    std::string str(const std::string& k, const std::string& def = "") const {
        auto it = values.find(k);
        return it == values.end() ? def : it->second;
    }
    double num(const std::string& k, double def = 0) const {
        auto it = values.find(k);
        return it == values.end() || it->second.empty() ? def : atof(it->second.c_str());
    }
    bool flag(const std::string& k) const { return str(k) == "true" || str(k) == "1"; }
};

inline void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

// Parses {"key": value, ...}. Nested objects and arrays are not accepted.
inline bool parse(const std::string& s, Object& out) {
    out.values.clear();
    size_t i = 0, n = s.size();
    auto ws = [&]() { while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++; };
    auto str = [&](std::string& v) -> bool {
        if (i >= n || s[i] != '"') return false;
        i++;
        v.clear();
        while (i < n && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { v += c; continue; }
            if (i >= n) return false;
            char e = s[i++];
            switch (e) {
                case 'n': v += '\n'; break;
                case 'r': v += '\r'; break;
                case 't': v += '\t'; break;
                case 'b': v += '\b'; break;
                case 'f': v += '\f'; break;
                case 'u': {
                    if (i + 4 > n) return false;
                    uint32_t cp = (uint32_t)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= n && s[i] == '\\' && s[i + 1] == 'u') {
                        uint32_t lo = (uint32_t)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(v, cp);
                    break;
                }
                default: v += e;
            }
        }
        if (i >= n) return false;
        i++;
        return true;
    };
    ws();
    if (i >= n || s[i] != '{') return false;
    i++;
    ws();
    if (i < n && s[i] == '}') return true;
    while (i < n) {
        std::string key, val;
        ws();
        if (!str(key)) return false;
        ws();
        if (i >= n || s[i] != ':') return false;
        i++;
        ws();
        if (i < n && s[i] == '"') {
            if (!str(val)) return false;
        } else {
            size_t start = i;
            while (i < n && s[i] != ',' && s[i] != '}' && s[i] != ' ') i++;
            val = s.substr(start, i - start);
            if (val.empty() || val[0] == '{' || val[0] == '[') return false;
        }
        out.values[key] = val;
        ws();
        if (i < n && s[i] == ',') { i++; continue; }
        if (i < n && s[i] == '}') return true;
        return false;
    }
    return false;
}

inline std::string quote(const std::string& v) {
    std::string out = "\"";
    for (unsigned char c : v) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += (char)c;
    }
    return out + "\"";
}

// Builds a JSON text step by step:
//   Writer w; w.obj().kv("a", 1).key("list").arr().val("x").end().end();
class Writer {
public:
    std::string out;
    Writer& obj() { open('{', '}'); return *this; }
    Writer& arr() { open('[', ']'); return *this; }
    Writer& end() {
        if (!stack_.empty()) { out += stack_.back(); stack_.pop_back(); }
        first_ = false;
        return *this;
    }
    Writer& key(const std::string& k) { sep(); out += quote(k) + ":"; afterKey_ = true; return *this; }
    Writer& val(const std::string& v) { sep(); out += quote(v); return *this; }
    Writer& val(const char* v) { return val(std::string(v ? v : "")); }
    Writer& val(bool v) { sep(); out += v ? "true" : "false"; return *this; }
    Writer& val(int v) { sep(); out += std::to_string(v); return *this; }
    Writer& val(long v) { sep(); out += std::to_string(v); return *this; }
    Writer& val(long long v) { sep(); out += std::to_string(v); return *this; }
    Writer& val(unsigned v) { sep(); out += std::to_string(v); return *this; }
    Writer& val(unsigned long v) { sep(); out += std::to_string(v); return *this; }
    Writer& val(unsigned long long v) { sep(); out += std::to_string(v); return *this; }
    Writer& val(float v) { return val((double)v); }
    Writer& val(double v) {
        sep();
        if (!std::isfinite(v)) { out += "null"; return *this; }
        char b[32];
        snprintf(b, sizeof(b), "%.7g", v);
        out += b;
        return *this;
    }
    Writer& raw(const std::string& r) { sep(); out += r; return *this; }
    template <class T> Writer& kv(const std::string& k, const T& v) { key(k); return val(v); }

private:
    void open(char o, char c) { sep(); out += o; stack_ += c; first_ = true; }
    void sep() {
        if (afterKey_) { afterKey_ = false; return; }
        if (!first_ && !stack_.empty()) out += ',';
        first_ = false;
    }
    bool first_ = true, afterKey_ = false;
    std::string stack_;
};

}  // namespace json
