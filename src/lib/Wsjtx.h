// WSJT-X network messages (the UDP "reporting" protocol, also sent by JTDX):
// status, decodes and logged contacts. Big-endian Qt serialisation.
// Written from the protocol description in the WSJT-X source
// (Network/NetworkMessage.hpp); pure C++, host-testable.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace wsjtx {

constexpr uint32_t MAGIC = 0xadbccbda;
enum Type { HEARTBEAT = 0, STATUS = 1, DECODE = 2, CLEAR = 3, QSO_LOGGED = 5, CLOSE = 6, LOGGED_ADIF = 12 };

class Reader {
public:
    Reader(const uint8_t* data, size_t len) : p_(data), end_(data + len) {}
    bool ok() const { return !bad_; }
    uint8_t u8() { return need(1) ? *p_++ : 0; }
    bool boolean() { return u8() != 0; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = ((uint32_t)p_[0] << 24) | ((uint32_t)p_[1] << 16) | ((uint32_t)p_[2] << 8) | p_[3];
        p_ += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t hi = u32();
        return (hi << 32) | u32();
    }
    double f64() {
        uint64_t bits = u64();
        double d;
        memcpy(&d, &bits, 8);
        return d;
    }
    std::string utf8() {                     // length 0xffffffff means "null string"
        uint32_t len = u32();
        if (len == 0xffffffff) return "";
        if (!need(len)) return "";
        std::string s((const char*)p_, len);
        p_ += len;
        return s;
    }

private:
    bool need(size_t n) {
        if (bad_ || (size_t)(end_ - p_) < n) { bad_ = true; return false; }
        return true;
    }
    const uint8_t* p_;
    const uint8_t* end_;
    bool bad_ = false;
};

// Builds messages (tests and the simulator).
class Writer {
public:
    std::string out;
    Writer(uint32_t type, const std::string& id = "WSJT-X") { u32(MAGIC); u32(2); u32(type); utf8(id); }
    void u8(uint8_t v) { out += (char)v; }
    void u32(uint32_t v) { for (int s = 24; s >= 0; s -= 8) out += (char)(v >> s); }
    void u64(uint64_t v) { u32((uint32_t)(v >> 32)); u32((uint32_t)v); }
    void f64(double d) { uint64_t bits; memcpy(&bits, &d, 8); u64(bits); }
    void utf8(const std::string& s) { u32((uint32_t)s.size()); out += s; }
};

struct Message {
    int type = -1;
    std::string id;
    // STATUS
    uint64_t dialHz = 0;
    std::string mode, dxCall, deCall, deGrid;
    bool txEnabled = false, transmitting = false, decoding = false;
    // DECODE
    bool isNew = false;
    uint32_t timeMs = 0;        // milliseconds since midnight UTC
    int snr = 0;
    double dt = 0;
    uint32_t df = 0;            // audio frequency, Hz
    std::string text;
    // LOGGED_ADIF
    std::string adif;
};

inline bool parse(const uint8_t* data, size_t len, Message& m) {
    Reader r(data, len);
    if (r.u32() != MAGIC) return false;
    uint32_t schema = r.u32();
    m = Message();
    m.type = (int)r.u32();
    m.id = r.utf8();
    if (!r.ok() || schema < 2 || schema > 3) return false;
    if (m.type == STATUS) {
        m.dialHz = r.u64();
        m.mode = r.utf8();
        m.dxCall = r.utf8();
        r.utf8();                           // report
        r.utf8();                           // tx mode
        m.txEnabled = r.boolean();
        m.transmitting = r.boolean();
        m.decoding = r.boolean();
        r.u32();                            // rx audio frequency
        r.u32();                            // tx audio frequency
        m.deCall = r.utf8();
        m.deGrid = r.utf8();
    } else if (m.type == DECODE) {
        m.isNew = r.boolean();
        m.timeMs = r.u32();
        m.snr = (int)(int32_t)r.u32();
        m.dt = r.f64();
        m.df = r.u32();
        m.mode = r.utf8();
        m.text = r.utf8();
    } else if (m.type == LOGGED_ADIF) {
        m.adif = r.utf8();
    }
    return r.ok();
}

// What an FT8/FT4 message says: "CQ DX JA1ABC PM95", "TA3BLD JA1ABC -12", "TA3BLD JA1ABC RR73".
struct Decoded {
    bool cq = false;
    std::string from, to, grid;
};

inline bool looksLikeCall(const std::string& s) {
    if (s.size() < 3 || s.size() > 11 || s[0] == '<') return false;
    bool digit = false, letter = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') digit = true;
        else if ((c >= 'A' && c <= 'Z') || c == '/') letter = true;
        else return false;
    }
    return digit && letter;
}

inline bool looksLikeGrid(const std::string& s) {
    return s.size() == 4 && s[0] >= 'A' && s[0] <= 'R' && s[1] >= 'A' && s[1] <= 'R' && s[2] >= '0' && s[2] <= '9' &&
           s[3] >= '0' && s[3] <= '9' && s != "RR73";
}

inline Decoded analyse(const std::string& text) {
    Decoded d;
    std::string words[5];
    int n = 0;
    for (size_t pos = 0; pos < text.size() && n < 5;) {
        size_t end = text.find(' ', pos);
        if (end == std::string::npos) end = text.size();
        if (end > pos) words[n++] = text.substr(pos, end - pos);
        pos = end + 1;
    }
    if (n == 0) return d;
    if (words[0] == "CQ") {
        d.cq = true;
        for (int i = 1; i < n; i++)             // skip "DX", "POTA", "EU", ... to the callsign
            if (looksLikeCall(words[i])) { d.from = words[i]; break; }
    } else if (n >= 2) {
        d.to = words[0];
        d.from = words[1];
    }
    if (n >= 2 && looksLikeGrid(words[n - 1])) d.grid = words[n - 1];
    return d;
}

}  // namespace wsjtx
