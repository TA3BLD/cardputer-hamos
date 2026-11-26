// RTTY: 45.45 baud, 170 Hz shift, 5-bit Baudot (ITA2, US figures), audio tones
// 2125 Hz mark / 2295 Hz space. Modulator and demodulator. Pure C++, host-testable.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace rtty {

constexpr int SAMPLE_RATE = 8000;
constexpr double BAUD = 45.45, MARK_HZ = 2125.0, SPACE_HZ = 2295.0, PI2 = 6.283185307179586;
constexpr int HOP = 22;                 // samples between decisions: 8 per bit
constexpr int WINDOW = 4 * HOP;         // 11 ms analysis window (resolves the 170 Hz shift)
constexpr uint8_t LTRS = 31, FIGS = 27;

inline const char* lettersTable() { return "\0E\nA SIU\rDRJNFCKTZLWHYPQOBG\0MXV"; }
inline const char* figuresTable() { return "\0003\n- '87\r$4',!:(5\")2#6019?&\0./;"; }

// Text -> Baudot codes with the shift characters inserted.
inline std::vector<uint8_t> encodeText(const std::string& text) {
    std::vector<uint8_t> out = {LTRS, LTRS};
    bool figures = false;
    for (char raw : text) {
        char c = (char)toupper((unsigned char)raw);
        int letter = -1, figure = -1;
        for (int i = 1; i < 32; i++) {
            if (i == LTRS || i == FIGS) continue;
            if (lettersTable()[i] == c) letter = i;
            if (figuresTable()[i] == c) figure = i;
        }
        if (letter >= 0 && (c == ' ' || c == '\r' || c == '\n')) {
            out.push_back((uint8_t)letter);
            if (c == ' ') figures = false;      // receivers unshift on space
            continue;
        }
        if (letter >= 0) {
            if (figures) { out.push_back(LTRS); figures = false; }
            out.push_back((uint8_t)letter);
        } else if (figure >= 0) {
            if (!figures) { out.push_back(FIGS); figures = true; }
            out.push_back((uint8_t)figure);
        }
    }
    return out;
}

// Generates the audio for a text piece by piece: idle mark, then for every
// character a start bit, 5 data bits (LSB first) and 1.5 stop bits.
class Modulator {
public:
    explicit Modulator(const std::string& text, int amplitude = 12000) : amplitude_((float)amplitude) {
        add(true, 10);
        for (uint8_t code : encodeText(text)) {
            add(false, 1);
            for (int i = 0; i < 5; i++) add((code >> i) & 1, 1);
            add(true, 1.5f);
        }
        add(true, 6);
    }

    // Writes up to `max` samples; returns 0 when the message is finished.
    size_t read(int16_t* out, size_t max) {
        const double samplesPerBit = SAMPLE_RATE / BAUD;
        size_t n = 0;
        while (n < max && run_ < runs_.size()) {
            if ((double)emitted_ >= end_) {
                if (++run_ < runs_.size()) end_ += runs_[run_].bits * samplesPerBit;
                continue;
            }
            out[n++] = (int16_t)(amplitude_ * std::sin(phase_));
            phase_ += (float)(PI2 * (runs_[run_].mark ? MARK_HZ : SPACE_HZ) / SAMPLE_RATE);
            if (phase_ > (float)PI2) phase_ -= (float)PI2;
            emitted_++;
        }
        return n;
    }

private:
    struct Run {
        bool mark;
        float bits;
    };
    void add(bool mark, float bits) {
        if (runs_.empty()) end_ = bits * (SAMPLE_RATE / BAUD);
        runs_.push_back({mark, bits});
    }
    std::vector<Run> runs_;
    size_t run_ = 0, emitted_ = 0;
    double end_ = 0;
    float phase_ = 0, amplitude_;
};

// The whole message at once (tests, short texts).
inline std::vector<int16_t> modulate(const std::string& text, int amplitude = 12000) {
    Modulator m(text, amplitude);
    std::vector<int16_t> out;
    int16_t chunk[512];
    for (size_t n; (n = m.read(chunk, 512)) > 0;) out.insert(out.end(), chunk, chunk + n);
    return out;
}

class Demodulator {
public:
    std::string text;           // decoded characters since the caller last cleared it
    bool reverse = false;       // swap mark and space (wrong sideband)
    float level = 0;            // 0..1, how clearly one tone dominates (tuning aid)

    void feed(const int16_t* samples, size_t n) {
        for (size_t k = 0; k < n; k++) {
            window_[head_] = samples[k];
            head_ = (head_ + 1) % WINDOW;
            if (++sinceHop_ < HOP) continue;
            sinceHop_ = 0;
            float m = power(MARK_HZ), s = power(SPACE_HZ);
            float total = m + s;
            level = 0.9f * level + 0.1f * (total > 0 ? std::fabs(m - s) / total : 0);
            bool signal = total > 1e6f * WINDOW;                // squelch: ignore silence
            decision(signal ? ((m > s) != reverse) : true);
        }
    }

private:
    float power(double hz) const {                              // Goertzel over the window
        float coeff = 2.0f * (float)std::cos(PI2 * hz / SAMPLE_RATE), s1 = 0, s2 = 0;
        for (int i = 0; i < WINDOW; i++) {
            float s0 = window_[(head_ + i) % WINDOW] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }

    // One mark/space decision every 1/8 bit: a small software UART.
    void decision(bool mark) {
        if (state_ == IDLE) {
            if (!mark) { state_ = START; count_ = 0; }
            return;
        }
        count_++;
        if (state_ == START) {
            if (mark && count_ < 4) { state_ = IDLE; return; }  // glitch, not a start bit
            if (count_ == 4) { state_ = DATA; count_ = 0; bitIndex_ = 0; code_ = 0; }   // middle of the start bit
            return;
        }
        if (count_ < 8) return;
        count_ = 0;
        if (state_ == DATA) {
            if (mark) code_ |= (uint8_t)(1 << bitIndex_);
            if (++bitIndex_ == 5) state_ = STOP;
            return;
        }
        if (mark) emit(code_);                                  // valid stop bit
        state_ = IDLE;
    }

    void emit(uint8_t code) {
        if (code == LTRS) { figures_ = false; return; }
        if (code == FIGS) { figures_ = true; return; }
        char c = (figures_ ? figuresTable() : lettersTable())[code];
        if (c == ' ') figures_ = false;                         // "unshift on space"
        if (c == '\r') return;
        if (c) text += c;
    }

    enum { IDLE, START, DATA, STOP } state_ = IDLE;
    float window_[WINDOW] = {0};
    int head_ = 0, sinceHop_ = 0, count_ = 0, bitIndex_ = 0;
    uint8_t code_ = 0;
    bool figures_ = false;
};

}  // namespace rtty
