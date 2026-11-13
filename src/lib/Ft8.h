// FT8 protocol: message packing (77 bits), CRC-14, LDPC(174,91) encoding,
// the 79 channel symbols and the transmit audio. Pure C++, host-testable.
//
// Written from the protocol description (Franke, Somerville, Taylor: "The FT4
// and FT8 Communication Protocols", QEX July/August 2020) and checked against
// the WSJT-X port in the Python "ft8lib" package: see tools/ft8/crosscheck.py.
//
// Messages handled: the standard ones ("CQ TA3BLD KM38", "CQ DX ...",
// "TA1ABC TA3BLD -12", "... R-12", "... RRR / RR73 / 73") and 13-character
// free text. Compound and special-event callsigns are received as "<...>"
// and cannot be transmitted.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "Ft8Tables.h"

namespace ft8 {

constexpr int kSymbols = 79;                // channel symbols per transmission
constexpr int kCodeBits = 174, kMessageBits = 91, kParityBits = 83;
constexpr float kToneSpacingHz = 6.25f, kSymbolSeconds = 0.16f;
constexpr float kTxSeconds = kSymbols * kSymbolSeconds;         // 12.64 s
constexpr int kCostas[7] = {3, 1, 4, 0, 6, 5, 2};               // synchronisation tones
constexpr int kGray[8] = {0, 1, 3, 2, 5, 6, 4, 7};              // 3 bits -> tone

constexpr uint32_t kTokens = 2063592, kMax22 = 4194304, kMaxGrid = 32400;

// ------------------------------------------------------------ bit fields ----

// Bits are numbered from the most significant bit of byte 0.
inline void putBits(uint8_t* buf, int pos, int len, uint64_t value) {
    for (int i = 0; i < len; i++) {
        int bit = pos + i;
        uint8_t mask = (uint8_t)(0x80 >> (bit & 7));
        if ((value >> (len - 1 - i)) & 1) buf[bit >> 3] |= mask;
        else buf[bit >> 3] &= (uint8_t)~mask;
    }
}

inline uint64_t getBits(const uint8_t* buf, int pos, int len) {
    uint64_t v = 0;
    for (int i = 0; i < len; i++) {
        int bit = pos + i;
        v = (v << 1) | ((buf[bit >> 3] >> (7 - (bit & 7))) & 1);
    }
    return v;
}

// ------------------------------------------------------------- callsigns ----

inline int indexIn(const char* alphabet, char c) {
    const char* p = strchr(alphabet, c);
    return (p && c) ? (int)(p - alphabet) : -1;
}

constexpr const char* kA1 = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr const char* kA2 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr const char* kA3 = "0123456789";
constexpr const char* kA4 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr const char* kTextAlphabet = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";

// A callsign, or one of the tokens DE / QRZ / CQ / "CQ_DX" / "CQ_123", as 28 bits.
// Returns false for anything that is not a standard callsign.
inline bool packCall(const std::string& word, uint32_t& n28) {
    if (word == "DE") { n28 = 0; return true; }
    if (word == "QRZ") { n28 = 1; return true; }
    if (word == "CQ") { n28 = 2; return true; }
    if (word.compare(0, 3, "CQ_") == 0 && word.size() >= 4 && word.size() <= 7) {
        std::string tail = word.substr(3);
        bool digits = tail.size() == 3, letters = true;
        for (char c : tail) {
            if (!isdigit((unsigned char)c)) digits = false;
            if (c < 'A' || c > 'Z') letters = false;
        }
        if (digits) { n28 = 3 + (uint32_t)atoi(tail.c_str()); return true; }
        if (!letters) return false;
        uint32_t m = 0;
        for (size_t i = 0; i < 4; i++) {                // right-aligned in four characters
            int pad = 4 - (int)tail.size();
            m = 27 * m + ((int)i < pad ? 0 : (uint32_t)(tail[i - (size_t)pad] - 'A' + 1));
        }
        n28 = 3 + 1000 + m;
        return true;
    }
    // Standard callsign: up to two characters (at least one letter), one digit,
    // one to three letters; for example K1ABC, TA3BLD, 9A1A, 4X1AB.
    size_t n = word.size();
    if (n < 3 || n > 6 || word[0] == 'Q') return false;
    size_t area = 0;                                    // 1-based position of the last digit
    for (size_t i = n; i >= 2; i--)
        if (isdigit((unsigned char)word[i - 1])) { area = i; break; }
    if (area < 2 || area > 3) return false;
    int prefixDigits = 0, prefixLetters = 0, suffixLetters = 0;
    for (size_t i = 0; i + 1 < area; i++) {
        if (isdigit((unsigned char)word[i])) prefixDigits++;
        else if (word[i] >= 'A' && word[i] <= 'Z') prefixLetters++;
        else return false;
    }
    for (size_t i = area; i < n; i++) {
        if (word[i] < 'A' || word[i] > 'Z') return false;
        suffixLetters++;
    }
    if (prefixLetters == 0 || prefixDigits >= (int)area - 1 || suffixLetters < 1 || suffixLetters > 3) return false;
    std::string c = area == 2 ? " " + word : word;
    c.resize(6, ' ');
    int i1 = indexIn(kA1, c[0]), i2 = indexIn(kA2, c[1]), i3 = indexIn(kA3, c[2]);
    int i4 = indexIn(kA4, c[3]), i5 = indexIn(kA4, c[4]), i6 = indexIn(kA4, c[5]);
    if (i1 < 0 || i2 < 0 || i3 < 0 || i4 < 0 || i5 < 0 || i6 < 0) return false;
    uint32_t v = (uint32_t)i1;
    v = v * 36 + (uint32_t)i2;
    v = v * 10 + (uint32_t)i3;
    v = v * 27 + (uint32_t)i4;
    v = v * 27 + (uint32_t)i5;
    v = v * 27 + (uint32_t)i6;
    n28 = v + kTokens + kMax22;
    return true;
}

// Compound and special callsigns travel as a hash. A receiver can only show
// one it already knows; HamOS knows its own.
inline uint32_t hashCall(const std::string& call, int bits) {
    std::string c = call;
    c.resize(11, ' ');
    uint64_t n = 0;
    for (char ch : c) {
        int j = indexIn(" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/", (char)toupper((unsigned char)ch));
        n = 38 * n + (uint64_t)(j < 0 ? 0 : j);
    }
    return (uint32_t)((47055833459ULL * n) >> (64 - bits));
}

inline std::string hashedName(uint32_t hash, int bits, const std::string& myCall) {
    return (!myCall.empty() && hashCall(myCall, bits) == hash) ? "<" + myCall + ">" : "<...>";
}

// 28 bits -> callsign or token ("CQ", "CQ DX", "<...>" for a hashed callsign).
// Returns "" for values that are not a valid callsign.
inline std::string unpackCall(uint32_t n28, const std::string& myCall = "") {
    if (n28 < kTokens) {
        if (n28 == 0) return "DE";
        if (n28 == 1) return "QRZ";
        if (n28 == 2) return "CQ";
        char buf[12];
        if (n28 <= 1002) {
            snprintf(buf, sizeof(buf), "CQ %03u", (unsigned)(n28 - 3));
            return buf;
        }
        if (n28 <= 532443) {
            uint32_t n = n28 - 1003;
            std::string tail(4, ' ');
            for (int i = 3; i >= 0; i--) { tail[(size_t)i] = kA4[n % 27]; n /= 27; }
            size_t first = tail.find_first_not_of(' ');
            return first == std::string::npos ? "" : "CQ " + tail.substr(first);
        }
        return "";
    }
    n28 -= kTokens;
    if (n28 < kMax22) return hashedName(n28, 22, myCall);
    uint32_t n = n28 - kMax22;
    char c[7];
    c[5] = kA4[n % 27]; n /= 27;
    c[4] = kA4[n % 27]; n /= 27;
    c[3] = kA4[n % 27]; n /= 27;
    c[2] = kA3[n % 10]; n /= 10;
    c[1] = kA2[n % 36]; n /= 36;
    if (n >= 37) return "";
    c[0] = kA1[n];
    c[6] = 0;
    std::string call(c);
    size_t a = call.find_first_not_of(' '), b = call.find_last_not_of(' ');
    if (a == std::string::npos) return "";
    call = call.substr(a, b - a + 1);
    // The same sanity rules a receiver applies: no inner blanks, a letter in the
    // prefix, the last digit in position 2 or 3, only letters after it.
    if (call.size() < 3 || call[0] == 'Q' || call.find(' ') != std::string::npos) return "";
    size_t area = 0;
    for (size_t i = call.size(); i >= 1; i--)
        if (isdigit((unsigned char)call[i - 1])) { area = i; break; }
    if (area < 2 || area > 3) return "";
    bool prefixLetter = false;
    for (size_t i = 0; i + 1 < area; i++) prefixLetter |= (call[i] >= 'A' && call[i] <= 'Z');
    if (!prefixLetter || area == call.size()) return "";
    return call;
}

inline bool isGrid4(const std::string& s) {
    return s.size() == 4 && s[0] >= 'A' && s[0] <= 'R' && s[1] >= 'A' && s[1] <= 'R' && isdigit((unsigned char)s[2]) &&
           isdigit((unsigned char)s[3]);
}

// "+07", "-12", "R-12" -> value and whether it carries the R.
inline bool parseReport(const std::string& s, int& db, bool& roger) {
    size_t i = 0;
    roger = !s.empty() && s[0] == 'R';
    if (roger) i = 1;
    if (s.size() != i + 3 || (s[i] != '+' && s[i] != '-') || !isdigit((unsigned char)s[i + 1]) ||
        !isdigit((unsigned char)s[i + 2]))
        return false;
    db = atoi(s.c_str() + i);
    return db >= -50 && db <= 49;
}

inline std::vector<std::string> splitWords(const std::string& text) {
    std::vector<std::string> words;
    std::string w;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i < text.size() && text[i] != ' ') {
            w += (char)toupper((unsigned char)text[i]);
        } else if (!w.empty()) {
            words.push_back(w);
            w.clear();
        }
    }
    return words;
}

// ------------------------------------------------------------- messages ----

// Free text: up to 13 characters as one base-42 number in 71 bits.
inline bool packFreeText(const std::string& text, uint8_t payload[10]) {
    std::string t = text;
    for (char& c : t) c = (char)toupper((unsigned char)c);
    if (t.size() > 13) return false;
    t.insert(0, 13 - t.size(), ' ');                    // right-aligned like WSJT-X
    uint8_t num[9] = {0};                               // 72-bit big-endian accumulator
    for (char c : t) {
        int digit = indexIn(kTextAlphabet, c);
        if (digit < 0) return false;
        unsigned carry = (unsigned)digit;
        for (int i = 8; i >= 0; i--) {
            unsigned v = (unsigned)num[i] * 42 + carry;
            num[i] = (uint8_t)v;
            carry = v >> 8;
        }
    }
    memset(payload, 0, 10);
    for (int bit = 0; bit < 71; bit++)                  // the low 71 bits, most significant first
        putBits(payload, bit, 1, (num[(bit + 1) >> 3] >> (7 - ((bit + 1) & 7))) & 1);
    return true;                                        // n3 = 0 and i3 = 0 are already zero
}

inline std::string unpackFreeText(const uint8_t payload[10]) {
    uint8_t num[9] = {0};
    for (int bit = 0; bit < 71; bit++)
        if (getBits(payload, bit, 1)) num[(bit + 1) >> 3] |= (uint8_t)(0x80 >> ((bit + 1) & 7));
    std::string out(13, ' ');
    for (int k = 12; k >= 0; k--) {
        unsigned rem = 0;
        for (int i = 0; i < 9; i++) {
            unsigned v = (rem << 8) | num[i];
            num[i] = (uint8_t)(v / 42);
            rem = v % 42;
        }
        out[(size_t)k] = kTextAlphabet[rem];
    }
    size_t a = out.find_first_not_of(' '), b = out.find_last_not_of(' ');
    return a == std::string::npos ? "" : out.substr(a, b - a + 1);
}

// Text made only of hexadecimal digits (up to 18) goes out as "telemetry",
// like WSJT-X does: the number itself in 71 bits.
inline bool packTelemetry(const std::string& text, uint8_t payload[10]) {
    if (text.empty() || text.size() > 18) return false;
    std::string t = text;
    t.insert(0, 18 - t.size(), '0');
    if (indexIn("01234567", t[0]) < 0) return false;    // 71 bits: the first digit has three
    memset(payload, 0, 10);
    for (size_t i = 0; i < 18; i++) {
        int v = indexIn("0123456789ABCDEF", (char)toupper((unsigned char)t[i]));
        if (v < 0) return false;
        if (i == 0) putBits(payload, 0, 3, (uint64_t)v);
        else putBits(payload, 3 + 4 * ((int)i - 1), 4, (uint64_t)v);
    }
    putBits(payload, 71, 3, 5);
    return true;
}

inline std::string unpackTelemetry(const uint8_t payload[10]) {
    std::string out;
    out += "0123456789ABCDEF"[getBits(payload, 0, 3)];
    for (int i = 0; i < 17; i++) out += "0123456789ABCDEF"[getBits(payload, 3 + 4 * i, 4)];
    size_t first = out.find_first_not_of('0');
    return first == std::string::npos ? "0" : out.substr(first);
}

// Text -> 77-bit payload (10 bytes, the last three bits unused).
inline bool pack(const std::string& text, uint8_t payload[10]) {
    std::vector<std::string> w = splitWords(text);
    if (w.empty()) return false;
    // "CQ DX TA3BLD KM38" / "CQ POTA TA3BLD" / "CQ 123 TA3BLD KM38": fold the
    // word after CQ into the token "CQ_DX". ("CQ K1ABC FN42" has no such word.)
    if (w[0] == "CQ" && (w.size() == 4 || (w.size() == 3 && !isGrid4(w[2])))) {
        uint32_t token;
        if (packCall("CQ_" + w[1], token)) {
            w[0] = "CQ_" + w[1];
            w.erase(w.begin() + 1);
        }
    }
    uint32_t a = 0, b = 0;
    if (w.size() >= 2 && w.size() <= 4 && packCall(w[0], a) && packCall(w[1], b) && b >= kTokens + kMax22) {
        bool roger = false;
        uint32_t grid = kMaxGrid + 1;                   // no third word
        bool ok = true;
        if (w.size() >= 3) {
            const std::string& last = w.back();
            int db = 0;
            bool r = false;
            if (w.size() == 4) {
                if (w[2] != "R" || !isGrid4(last)) ok = false;
                roger = true;
            }
            if (last == "RRR") grid = kMaxGrid + 2;
            else if (last == "73") grid = kMaxGrid + 4;
            else if (isGrid4(last))                     // includes "RR73": WSJT-X sends it as a locator
                grid = (uint32_t)(((last[0] - 'A') * 18 + (last[1] - 'A')) * 10 + (last[2] - '0')) * 10 + (uint32_t)(last[3] - '0');
            else if (parseReport(last, db, r)) {
                roger = r;
                if (db >= -50 && db <= -31) db += 101;
                grid = kMaxGrid + (uint32_t)(db + 35);
            } else {
                ok = false;
            }
        }
        if (ok) {
            memset(payload, 0, 10);
            putBits(payload, 0, 28, a);
            putBits(payload, 29, 28, b);                // bits 28 and 57: "/R" flags, not used
            putBits(payload, 58, 1, roger ? 1 : 0);
            putBits(payload, 59, 15, grid);
            putBits(payload, 74, 3, 1);                 // message type 1
            return true;
        }
    }
    if (w.size() == 1 && packTelemetry(w[0], payload)) return true;
    return packFreeText(text, payload);
}

// Payload -> text; "" for message types that are not supported or not valid.
// `myCall` lets a hashed callsign be shown when it is our own.
inline std::string unpack(const uint8_t payload[10], const std::string& myCall = "") {
    int i3 = (int)getBits(payload, 74, 3), n3 = (int)getBits(payload, 71, 3);
    if (i3 == 0 && n3 == 0) return unpackFreeText(payload);
    if (i3 == 0 && n3 == 5) return unpackTelemetry(payload);
    if (i3 == 1 || i3 == 2) {
        std::string c1 = unpackCall((uint32_t)getBits(payload, 0, 28), myCall);
        std::string c2 = unpackCall((uint32_t)getBits(payload, 29, 28), myCall);
        if (c1.empty() || c2.empty()) return "";
        const char* suffix = i3 == 1 ? "/R" : "/P";
        if (getBits(payload, 28, 1) && c1[0] != '<' && c1.compare(0, 2, "CQ") != 0) c1 += suffix;
        if (getBits(payload, 57, 1) && c2[0] != '<') c2 += suffix;
        bool roger = getBits(payload, 58, 1);
        uint32_t grid = (uint32_t)getBits(payload, 59, 15);
        std::string out = c1 + " " + c2;
        if (grid <= kMaxGrid) {
            char g[5] = {(char)('A' + grid / 1800), (char)('A' + grid / 100 % 18), (char)('0' + grid / 10 % 10),
                         (char)('0' + grid % 10), 0};
            return out + (roger ? " R " : " ") + g;
        }
        uint32_t r = grid - kMaxGrid;
        if (r == 1) return out;
        if (r == 2) return out + " RRR";
        if (r == 3) return out + " RR73";
        if (r == 4) return out + " 73";
        int db = (int)r - 35;
        if (db > 50) db -= 101;
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%+03d", roger ? "R" : "", db);
        return out + " " + buf;
    }
    if (i3 == 4) {                                      // one hashed and one spelled-out callsign
        uint64_t n = getBits(payload, 12, 58);
        std::string call(11, ' ');
        for (int i = 10; i >= 0; i--) { call[(size_t)i] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/"[n % 38]; n /= 38; }
        size_t a = call.find_first_not_of(' '), b = call.find_last_not_of(' ');
        if (a == std::string::npos) return "";
        call = call.substr(a, b - a + 1);
        if (getBits(payload, 73, 1)) return "CQ " + call;
        static const char* tail[4] = {"", " RRR", " RR73", " 73"};
        std::string rest = tail[getBits(payload, 71, 2)];
        std::string hashed = hashedName((uint32_t)getBits(payload, 0, 12), 12, myCall);
        return (getBits(payload, 70, 1) ? call + " " + hashed : hashed + " " + call) + rest;
    }
    return "";
}

// --------------------------------------------------------------- coding ----

// CRC-14 of the 77 payload bits (zero-extended to 82 bits, as WSJT-X does).
inline int crc14(const uint8_t payload[10]) {
    int reg = 0;
    for (int i = 0; i < 82 + 14; i++) {
        int in = i < 77 ? (int)getBits(payload, i, 1) : 0;
        int top = (reg >> 13) & 1;
        reg = ((reg << 1) & 0x3FFF) | in;
        if (top) reg ^= 0x2757;
    }
    return reg;
}

// Payload -> the 174 codeword bits (one bit per byte): 77 message, 14 CRC, 83 parity.
inline void codeword(const uint8_t payload[10], uint8_t bits[kCodeBits]) {
    uint8_t msg[12] = {0};
    memcpy(msg, payload, 10);
    msg[9] &= 0xF8;
    putBits(msg, 77, 14, (uint64_t)crc14(payload));
    for (int i = 0; i < kMessageBits; i++) bits[i] = (uint8_t)getBits(msg, i, 1);
    for (int j = 0; j < kParityBits; j++) {
        int ones = 0;
        for (int k = 0; k < 12; k++) {
            uint8_t v = (uint8_t)(kGenerator[j][k] & msg[k]);
            while (v) { ones++; v &= (uint8_t)(v - 1); }
        }
        bits[kMessageBits + j] = (uint8_t)(ones & 1);
    }
}

// Payload -> the 79 tones (0..7): three 7-symbol sync blocks around two halves of 29 data symbols.
inline void tones(const uint8_t payload[10], uint8_t out[kSymbols]) {
    uint8_t bits[kCodeBits];
    codeword(payload, bits);
    int bit = 0;
    for (int i = 0; i < kSymbols; i++) {
        if (i < 7) out[i] = (uint8_t)kCostas[i];
        else if (i >= 36 && i < 43) out[i] = (uint8_t)kCostas[i - 36];
        else if (i >= 72) out[i] = (uint8_t)kCostas[i - 72];
        else {
            int v = (bits[bit] << 2) | (bits[bit + 1] << 1) | bits[bit + 2];
            out[i] = (uint8_t)kGray[v];
            bit += 3;
        }
    }
}

// ---------------------------------------------------------------- audio ----

// Transmit audio, generated piece by piece: 8-tone frequency-shift keying with
// Gaussian-smoothed transitions (BT = 2) and short ramps at both ends.
class Synth {
public:
    Synth(const uint8_t toneList[kSymbols], float baseHz, int sampleRate, int amplitude = 12000, float leadSilenceSeconds = 0)
        : baseHz_(baseHz), rate_((float)sampleRate), amplitude_((float)amplitude) {
        memcpy(tones_, toneList, kSymbols);
        perSymbol_ = (int)(sampleRate * kSymbolSeconds + 0.5f);
        total_ = (size_t)perSymbol_ * kSymbols;
        silence_ = (size_t)(leadSilenceSeconds * sampleRate);
    }

    size_t totalSamples() const { return silence_ + total_; }

    // Writes up to `max` samples; returns 0 when the transmission is complete.
    size_t read(int16_t* out, size_t max) {
        size_t n = 0;
        while (n < max && silence_ > 0) { out[n++] = 0; silence_--; }
        const float twoPi = 6.2831853f;
        const float c = 3.14159265f * std::sqrt(2.0f / std::log(2.0f)) * 2.0f;     // BT = 2
        size_t ramp = (size_t)perSymbol_ / 8;
        while (n < max && pos_ < total_) {
            int k = (int)(pos_ / (size_t)perSymbol_);
            float x = (float)(pos_ % (size_t)perSymbol_) / (float)perSymbol_ - 0.5f;   // -0.5 .. 0.5 within the symbol
            // Smoothed tone number: the neighbouring symbols blend in near the edges.
            float tone = 0;
            for (int d = -1; d <= 1; d++) {
                int j = k + d;
                float t = (float)tones_[j < 0 ? 0 : j >= kSymbols ? kSymbols - 1 : j];
                float u = x - (float)d;
                tone += t * 0.5f * (std::erf(c * (u + 0.5f)) - std::erf(c * (u - 0.5f)));
            }
            float envelope = 1.0f;
            if (pos_ < ramp) envelope = 0.5f * (1.0f - std::cos(3.14159265f * (float)pos_ / (float)ramp));
            else if (total_ - pos_ <= ramp) envelope = 0.5f * (1.0f - std::cos(3.14159265f * (float)(total_ - pos_) / (float)ramp));
            out[n++] = (int16_t)(amplitude_ * envelope * std::sin(phase_));
            phase_ += twoPi * (baseHz_ + tone * kToneSpacingHz) / rate_;
            if (phase_ > twoPi) phase_ -= twoPi;
            pos_++;
        }
        return n;
    }

private:
    uint8_t tones_[kSymbols];
    float baseHz_, rate_, amplitude_, phase_ = 0;
    int perSymbol_ = 0;
    size_t total_ = 0, pos_ = 0, silence_ = 0;
};

}  // namespace ft8
