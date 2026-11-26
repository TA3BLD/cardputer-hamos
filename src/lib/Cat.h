// Radio control protocols ("CAT"), without any hardware: building commands and
// reading answers.
//
//   Icom CI-V:   FE FE <radio> <E0> <cmd> [sub] [data] FD. Frequencies are BCD,
//                least significant byte first. On the one-wire bus we also
//                receive our own frames back: they are skipped.
//   Yaesu "817": FT-817/857/897 five-byte binary commands.
//   Kenwood:     ASCII "FA00014074000;" "MD2;" (also Elecraft, and the newer
//   Yaesu:       ASCII like Kenwood with other digit counts and mode letters
//                (FT-991, FT-891, FT-710, FTDX10).
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

namespace cat {

enum Protocol { CIV, FT817, KENWOOD, YAESU };

// Mode names are the logbook's: SSB, CW, AM, FM, RTTY, DATA.
// ---------------------------------------------------------------- CI-V ----
namespace civ {
constexpr uint8_t kController = 0xE0;

inline std::vector<uint8_t> frame(uint8_t radio, std::initializer_list<uint8_t> body) {
    std::vector<uint8_t> f = {0xFE, 0xFE, radio, kController};
    f.insert(f.end(), body.begin(), body.end());
    f.push_back(0xFD);
    return f;
}
inline std::vector<uint8_t> readFreq(uint8_t radio) { return frame(radio, {0x03}); }
inline std::vector<uint8_t> readMode(uint8_t radio) { return frame(radio, {0x04}); }
inline std::vector<uint8_t> setFreq(uint8_t radio, uint64_t hz) {
    std::vector<uint8_t> f = {0xFE, 0xFE, radio, kController, 0x05};
    for (int i = 0; i < 5; i++) {              // 10 digits, two per byte, low first
        uint8_t lo = hz % 10;
        hz /= 10;
        uint8_t hi = hz % 10;
        hz /= 10;
        f.push_back((uint8_t)(hi << 4 | lo));
    }
    f.push_back(0xFD);
    return f;
}
// Icom mode numbers: 00 LSB 01 USB 02 AM 03 CW 04 RTTY 05 FM 07 CW-R 08 RTTY-R
inline uint8_t modeCode(const std::string& m, double mhz) {
    if (m == "CW") return 0x03;
    if (m == "AM") return 0x02;
    if (m == "FM") return 0x05;
    if (m == "RTTY") return 0x04;
    if (m == "SSB") return mhz < 10 ? 0x00 : 0x01;
    return 0x01;                                // data modes: USB (+ data mode on)
}
inline bool isDataMode(const std::string& m) {
    return !(m.empty() || m == "CW" || m == "AM" || m == "FM" || m == "RTTY" || m == "SSB");
}
inline std::vector<uint8_t> setMode(uint8_t radio, uint8_t code) { return frame(radio, {0x06, code, 0x01}); }
// Data mode (USB-D) on / off: 1A 06 <on> <filter> (IC-7300, IC-705, IC-9700 ...)
inline std::vector<uint8_t> setData(uint8_t radio, bool on) { return frame(radio, {0x1A, 0x06, (uint8_t)(on ? 1 : 0), (uint8_t)(on ? 1 : 0)}); }
inline std::vector<uint8_t> ptt(uint8_t radio, bool on) { return frame(radio, {0x1C, 0x00, (uint8_t)(on ? 1 : 0)}); }
// Sends text in CW with the radio's own keyer (command 17, up to 30 characters).
inline std::vector<uint8_t> sendCw(uint8_t radio, const std::string& text) {
    std::vector<uint8_t> f = {0xFE, 0xFE, radio, kController, 0x17};
    for (size_t i = 0; i < text.size() && i < 30; i++) {
        char c = (char)toupper((unsigned char)text[i]);
        f.push_back((uint8_t)c);
    }
    f.push_back(0xFD);
    return f;
}

inline std::string modeName(uint8_t code) {
    switch (code) {
        case 0x00: case 0x01: return "SSB";
        case 0x02: return "AM";
        case 0x03: case 0x07: return "CW";
        case 0x04: case 0x08: return "RTTY";
        case 0x05: return "FM";
        default: return "";
    }
}

inline uint64_t bcdFreq(const uint8_t* p, int bytes) {
    uint64_t hz = 0, mul = 1;
    for (int i = 0; i < bytes; i++) {
        hz += (uint64_t)(p[i] & 0x0F) * mul;
        mul *= 10;
        hz += (uint64_t)(p[i] >> 4) * mul;
        mul *= 10;
    }
    return hz;
}

// Collects bytes into frames; answers frequency / mode reports.
struct Reader {
    std::vector<uint8_t> buf;
    uint64_t freqHz = 0;
    std::string mode;
    bool ok = false, ng = false;                // last FB / FA reply

    // Returns true when a frame from the radio changed frequency or mode.
    bool feed(uint8_t b) {
        if (buf.empty() && b != 0xFE) return false;
        if (buf.size() == 1 && b != 0xFE) { buf.clear(); return false; }
        if (buf.size() >= 2 && buf.size() < 3 && b == 0xFE) return false;     // extra preamble byte
        buf.push_back(b);
        if (buf.size() > 40) { buf.clear(); return false; }
        if (b != 0xFD) return false;
        std::vector<uint8_t> f;
        f.swap(buf);
        if (f.size() < 6) return false;
        uint8_t from = f[3], cmd = f[4];
        if (from == kController) return false;  // our own frame, echoed on the bus
        const uint8_t* data = f.data() + 5;
        int n = (int)f.size() - 6;
        if (cmd == 0xFB) { ok = true; return false; }
        if (cmd == 0xFA) { ng = true; return false; }
        if ((cmd == 0x03 || cmd == 0x00) && n >= 4) {     // frequency (00: transceive broadcast)
            freqHz = bcdFreq(data, n >= 5 ? 5 : 4);
            return true;
        }
        if ((cmd == 0x04 || cmd == 0x01) && n >= 1) {
            mode = modeName(data[0]);
            return true;
        }
        return false;
    }
};
}  // namespace civ

// ------------------------------------------------------- Yaesu FT-817 ----
namespace ft817 {
inline std::vector<uint8_t> readFreqMode() { return {0, 0, 0, 0, 0x03}; }
inline std::vector<uint8_t> setFreq(uint64_t hz) {
    uint64_t v = hz / 10;                       // 10 Hz steps, 8 digits, most significant first
    std::vector<uint8_t> f(5);
    for (int i = 3; i >= 0; i--) {
        uint8_t lo = v % 10;
        v /= 10;
        uint8_t hi = v % 10;
        v /= 10;
        f[i] = (uint8_t)(hi << 4 | lo);
    }
    f[4] = 0x01;
    return f;
}
// 00 LSB 01 USB 02 CW 03 CWR 04 AM 08 FM 0A DIG 0C PKT
inline std::vector<uint8_t> setMode(const std::string& m, double mhz) {
    uint8_t c = m == "CW" ? 0x02 : m == "AM" ? 0x04 : m == "FM" ? 0x08 : m == "SSB" ? (mhz < 10 ? 0x00 : 0x01) : 0x0A;
    return {c, 0, 0, 0, 0x07};
}
inline std::vector<uint8_t> ptt(bool on) { return {0, 0, 0, 0, (uint8_t)(on ? 0x08 : 0x88)}; }
inline std::string modeName(uint8_t c) {
    switch (c & 0x7F) {
        case 0x00: case 0x01: return "SSB";
        case 0x02: case 0x03: return "CW";
        case 0x04: return "AM";
        case 0x06: case 0x08: case 0x88: return "FM";
        case 0x0A: case 0x0C: return "DATA";
        default: return "";
    }
}
// The 5-byte answer to readFreqMode().
inline bool parse(const uint8_t* r, uint64_t& hz, std::string& mode) {
    uint64_t v = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t hi = r[i] >> 4, lo = r[i] & 0x0F;
        if (hi > 9 || lo > 9) return false;
        v = v * 100 + hi * 10 + lo;
    }
    hz = v * 10;
    mode = modeName(r[4]);
    return hz > 0;
}
}  // namespace ft817

// ---------------------------------------------- Kenwood / new Yaesu ----
namespace ascii {
inline std::string readFreq() { return "FA;"; }
inline std::string readMode() { return "MD;"; }  // Yaesu answers "MD0x;"
inline std::string readModeYaesu() { return "MD0;"; }
inline std::string setFreq(uint64_t hz, int digits) {
    std::string s = std::to_string(hz);
    if ((int)s.size() < digits) s = std::string((size_t)(digits - (int)s.size()), '0') + s;
    return "FA" + s + ";";
}
// Kenwood: 1 LSB 2 USB 3 CW 4 FM 5 AM 6 FSK 7 CW-R 9 FSK-R
inline std::string setModeKenwood(const std::string& m, double mhz) {
    char c = m == "CW" ? '3' : m == "FM" ? '4' : m == "AM" ? '5' : m == "RTTY" ? '6' : (m == "SSB" && mhz < 10) ? '1' : '2';
    return std::string("MD") + c + ";";
}
// Yaesu: 1 LSB 2 USB 3 CW-U 4 FM 5 AM 6 RTTY-L 7 CW-L 8 DATA-L 9 RTTY-U A DATA-FM B FM-N C DATA-U
inline std::string setModeYaesu(const std::string& m, double mhz) {
    char c = m == "CW" ? '3' : m == "FM" ? '4' : m == "AM" ? '5' : m == "RTTY" ? '6' : m == "SSB" ? (mhz < 10 ? '1' : '2') : 'C';
    return std::string("MD0") + c + ";";
}
inline std::string ptt(bool on, bool yaesu) { return yaesu ? (on ? "TX1;" : "TX0;") : (on ? "TX;" : "RX;"); }
// Kenwood / Elecraft: text for the radio's keyer, 24 characters per command.
// (The newer Yaesus have no command for free text.)
inline std::string sendCw(const std::string& text) {
    std::string t;
    for (char c : text) t += (char)toupper((unsigned char)c);
    if (t.size() > 24) t.resize(24);
    return "KY " + t + std::string(24 - t.size(), ' ') + ";";
}

inline std::string modeName(char c, bool yaesu) {
    if (yaesu) {
        switch (c) {
            case '1': case '2': return "SSB";
            case '3': case '7': return "CW";
            case '4': case 'B': return "FM";
            case '5': case 'D': return "AM";
            case '6': case '9': return "RTTY";
            case '8': case 'A': case 'C': return "DATA";
            default: return "";
        }
    }
    switch (c) {
        case '1': case '2': return "SSB";
        case '3': case '7': return "CW";
        case '4': return "FM";
        case '5': return "AM";
        case '6': case '9': return "RTTY";
        default: return "";
    }
}

// Splits replies at ';'.
struct Reader {
    std::string line;
    uint64_t freqHz = 0;
    int digits = 11;                            // as the radio answered FA
    std::string mode;
    bool yaesu = false;
    bool feed(char c) {
        if (c == '\r' || c == '\n') return false;
        if (c != ';') {
            if (line.size() < 40) line += c;
            return false;
        }
        std::string l;
        l.swap(line);
        if (l.size() > 2 && l[0] == 'F' && l[1] == 'A') {
            std::string d = l.substr(2);
            if (d.find_first_not_of("0123456789") != std::string::npos) return false;
            freqHz = strtoull(d.c_str(), nullptr, 10);
            digits = (int)d.size();
            return true;
        }
        if (l.size() >= 3 && l[0] == 'M' && l[1] == 'D') {
            mode = modeName(l.back(), yaesu);
            return true;
        }
        return false;
    }
};
}  // namespace ascii

// ------------------------------------------------------------- IC-705 BLE ----
// The IC-705 accepts CI-V over Bluetooth LE after a handshake on its serial
// service: our id (36 characters), our name, a token. It shows the name in its
// pairing list. Replies: F1 00 62 (name ok), F1 00 63 01/00 (newly paired /
// known), F1 00 64 (CI-V access granted).
namespace ic705 {
constexpr const char* kService = "14cf8001-1ec2-d408-1b04-2eb270f14203";
constexpr const char* kChar = "14cf8002-1ec2-d408-1b04-2eb270f14203";
inline std::vector<uint8_t> hello(uint8_t what, const std::string& text) {
    std::vector<uint8_t> f = {0xFE, 0xF1, 0x00, what};
    f.insert(f.end(), text.begin(), text.end());
    f.push_back(0xFD);
    return f;
}
inline std::vector<uint8_t> sendId(const std::string& uuid36) { return hello(0x61, uuid36); }
inline std::vector<uint8_t> sendName(const std::string& name) { return hello(0x62, name); }
inline std::vector<uint8_t> sendToken() { return {0xFE, 0xF1, 0x00, 0x63, 0xEE, 0x39, 0x09, 0x10, 0xFD}; }
// A per-device id in UUID form, from the chip's MAC address.
inline std::string deviceId(uint64_t mac) {
    char b[40];
    snprintf(b, sizeof(b), "48414D4F-5300-4000-8000-%012llX", (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
    return b;
}
// 0: not a handshake reply, else 0x62 / 0x63 / 0x64
inline int handshakeReply(const uint8_t* d, size_t n) {
    if (n >= 5 && d[0] == 0xFE && d[1] == 0xF1 && d[2] == 0x00 && d[n - 1] == 0xFD) return d[3];
    return 0;
}
}  // namespace ic705

}  // namespace cat
