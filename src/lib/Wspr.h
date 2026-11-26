// WSPR beacons (type 1 messages: callsign, 4-character locator, power in dBm):
// 162 symbols of 4-FSK, 1.4648 Hz apart, 0.683 s each, starting one second
// after an even minute. Pure C++; tones checked against ft8lib.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace wspr {

constexpr int kSymbols = 162;
constexpr float kToneSpacingHz = 12000.0f / 8192.0f;
constexpr float kSymbolSeconds = 8192.0f / 12000.0f;
constexpr uint8_t kSync[kSymbols] = {
        1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0,
        0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0,
        0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0};

// Power levels WSPR allows (dBm); others are rounded down to one of these.
inline int validPower(int dbm) {
    static const int levels[] = {0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60};
    int best = 0;
    for (int l : levels)
        if (l <= dbm) best = l;
    return best;
}

// Message -> 162 tones (0..3). False for a call or locator WSPR type 1 cannot carry.
inline bool tones(const std::string& callIn, const std::string& gridIn, int dbm, uint8_t out[kSymbols]) {
    std::string call;
    for (char c : callIn) call += (char)toupper((unsigned char)c);
    if (call.size() > 6 || call.size() < 3) return false;
    if (!isdigit((unsigned char)call[2])) call = " " + call;      // the area digit goes in the third place
    if (call.size() > 6 || !isdigit((unsigned char)call[2])) return false;
    while (call.size() < 6) call += ' ';
    auto code = [](char c) { return isdigit((unsigned char)c) ? c - '0' : c == ' ' ? 36 : c - 'A' + 10; };
    for (int i = 0; i < 6; i++)
        if (!(isalnum((unsigned char)call[i]) || call[i] == ' ')) return false;
    if (call[1] == ' ') return false;
    for (int i = 3; i < 6; i++)
        if (isdigit((unsigned char)call[i])) return false;
    uint32_t n = (uint32_t)code(call[0]);
    n = n * 36 + (uint32_t)code(call[1]);
    n = n * 10 + (uint32_t)code(call[2]);
    for (int i = 3; i < 6; i++) n = n * 27 + (uint32_t)(call[i] == ' ' ? 26 : call[i] - 'A');

    std::string g = gridIn.substr(0, 4);
    if (g.size() != 4) return false;
    g[0] = (char)toupper((unsigned char)g[0]);
    g[1] = (char)toupper((unsigned char)g[1]);
    if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' || !isdigit((unsigned char)g[2]) || !isdigit((unsigned char)g[3]))
        return false;
    uint32_t m = (uint32_t)((179 - 10 * (g[0] - 'A') - (g[2] - '0')) * 180 + 10 * (g[1] - 'A') + (g[3] - '0'));
    m = m * 128 + (uint32_t)validPower(dbm) + 64;

    uint8_t data[11] = {0};
    data[0] = (uint8_t)(n >> 20);
    data[1] = (uint8_t)(n >> 12);
    data[2] = (uint8_t)(n >> 4);
    data[3] = (uint8_t)(((n & 0x0F) << 4) | ((m >> 18) & 0x0F));
    data[4] = (uint8_t)(m >> 10);
    data[5] = (uint8_t)(m >> 2);
    data[6] = (uint8_t)((m & 0x03) << 6);

    // Convolutional code K=32, rate 1/2, over 81 bits (50 data + 31 zeros).
    uint8_t coded[kSymbols];
    uint32_t reg = 0;
    int k = 0;
    for (int i = 0; i < 81; i++) {
        int bit = (data[i / 8] >> (7 - i % 8)) & 1;
        reg = (reg << 1) | (uint32_t)bit;
        coded[k++] = (uint8_t)(__builtin_popcount(reg & 0xF2D05351u) & 1);
        coded[k++] = (uint8_t)(__builtin_popcount(reg & 0xE4613C47u) & 1);
    }
    // Interleave by bit-reversed index.
    uint8_t inter[kSymbols];
    int p = 0;
    for (int i = 0; i < 256 && p < kSymbols; i++) {
        int j = 0;
        for (int b = 0; b < 8; b++)
            if (i & (1 << b)) j |= 1 << (7 - b);
        if (j < kSymbols) inter[j] = coded[p++];
    }
    for (int i = 0; i < kSymbols; i++) out[i] = (uint8_t)(kSync[i] + 2 * inter[i]);
    return true;
}

// Phase-continuous 4-FSK audio.
class Synth {
public:
    Synth(const uint8_t t[kSymbols], float baseHz, int rate, int amplitude = 12000, float leadSeconds = 0)
        : base_(baseHz), rate_((float)rate), amp_((float)amplitude) {
        memcpy(t_, t, kSymbols);
        perSymbol_ = rate * kSymbolSeconds;
        total_ = (size_t)(perSymbol_ * kSymbols);
        silence_ = (size_t)(leadSeconds * rate);
    }
    size_t read(int16_t* out, size_t max) {
        size_t n = 0;
        while (n < max && silence_) { out[n++] = 0; silence_--; }
        while (n < max && pos_ < total_) {
            int k = (int)(pos_ / perSymbol_);
            out[n++] = (int16_t)(amp_ * std::sin(phase_));
            phase_ += 6.2831853f * (base_ + t_[k] * kToneSpacingHz) / rate_;
            if (phase_ > 6.2831853f) phase_ -= 6.2831853f;
            pos_++;
        }
        return n;
    }
    float progress() const { return total_ ? (float)pos_ / total_ : 1; }

private:
    uint8_t t_[kSymbols];
    float base_, rate_, amp_, perSymbol_ = 0, phase_ = 0;
    size_t total_ = 0, pos_ = 0, silence_ = 0;
};

}  // namespace wspr
