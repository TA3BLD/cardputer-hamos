// Base64 (standard alphabet, with padding). Pure C++, host-testable.
#pragma once
#include <cstdint>
#include <string>

namespace base64 {

inline std::string encode(const uint8_t* data, size_t len) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16 | (i + 1 < len ? (uint32_t)data[i + 1] << 8 : 0) | (i + 2 < len ? data[i + 2] : 0);
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += i + 1 < len ? t[v >> 6 & 63] : '=';
        out += i + 2 < len ? t[v & 63] : '=';
    }
    return out;
}

inline std::string decode(const std::string& in) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)(acc >> bits & 0xFF); }
    }
    return out;
}

}  // namespace base64
