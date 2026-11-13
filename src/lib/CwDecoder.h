// Morse code decoder for audio: tone detection (Goertzel) with an adaptive
// threshold and automatic speed tracking. Pure C++, host-testable.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "Morse.h"

namespace cw {

constexpr int SAMPLE_RATE = 8000;
constexpr int BLOCK = 40;               // 5 ms per decision

class Decoder {
public:
    std::string text;                   // decoded characters since the caller last cleared it
    bool keyDown = false;               // tone currently present
    float level = 0;                    // 0..1 signal strength relative to the recent peak

    explicit Decoder(double toneHz = 700.0) { setTone(toneHz); }

    void setTone(double hz) { coeff_ = (float)(2.0 * std::cos(6.283185307179586 * hz / SAMPLE_RATE)); }
    int wpm() const { return dotMs_ > 0 ? (int)(1200.0 / dotMs_ + 0.5) : 0; }

    void feed(const int16_t* samples, size_t n) {
        for (size_t k = 0; k < n; k++) {
            float s0 = samples[k] + coeff_ * s1_ - s2_;
            s2_ = s1_;
            s1_ = s0;
            if (++inBlock_ < BLOCK) continue;
            double mag = std::sqrt(std::fabs((double)s1_ * s1_ + (double)s2_ * s2_ - (double)coeff_ * s1_ * s2_)) / BLOCK;
            s1_ = s2_ = 0;
            inBlock_ = 0;
            block(mag);
        }
    }

private:
    void block(double mag) {
        // A slowly decaying peak, and the average level of everything well below
        // it (the gaps between the tones: noise). Without a clear distance
        // between the two nothing is decoded, so plain noise does not turn into
        // random letters. This also settles when listening starts mid-signal.
        peak_ = mag > peak_ ? mag : peak_ * 0.999;
        if (mag < 0.5 * peak_) noise_ = 0.95 * noise_ + 0.05 * mag;
        level = (float)(peak_ > 0 ? mag / peak_ : 0);
        if (warmup_ > 0) {                                      // let the levels settle first
            warmup_--;
            return;
        }
        bool strong = peak_ > 5.0 * noise_ + 50.0;
        bool raw = strong && mag > noise_ + 0.4 * (peak_ - noise_);

        // Two consecutive blocks must agree (10 ms) before the key state changes.
        if (raw == pending_) agree_++;
        else { pending_ = raw; agree_ = 1; }
        double step = 1000.0 * BLOCK / SAMPLE_RATE;
        durationMs_ += step;
        if (agree_ == 2 && pending_ != keyDown) {
            double length = durationMs_ - 2 * step;             // the new state began two blocks ago
            if (keyDown) mark(length);
            keyDown = pending_;
            durationMs_ = 2 * step;
            flushed_ = wordSent_ = false;
        }
        if (!keyDown) {                                         // silence: finish the letter / word
            if (!flushed_ && durationMs_ > 2.0 * dotMs_) { flush(); flushed_ = true; }
            if (!wordSent_ && durationMs_ > 5.0 * dotMs_) {
                if (needSpace_) text += ' ';
                needSpace_ = false;
                wordSent_ = true;
            }
        }
    }

    void mark(double ms) {
        if (ms < 12) return;                                    // click
        // Follow the sender's speed from the last few elements: once both short
        // and long ones have been seen, a dot is one unit and a dash three.
        recent_[recentNext_] = ms;
        recentNext_ = (recentNext_ + 1) % 8;
        if (recentCount_ < 8) recentCount_++;
        double shortest = 1e9, longest = 0;
        for (int i = 0; i < recentCount_; i++) {
            if (recent_[i] < shortest) shortest = recent_[i];
            if (recent_[i] > longest) longest = recent_[i];
        }
        if (longest >= 2.0 * shortest) dotMs_ = 0.5 * dotMs_ + 0.5 * (shortest + longest / 3.0) / 2.0;
        if (dotMs_ < 20) dotMs_ = 20;                           // 60 WPM
        if (dotMs_ > 240) dotMs_ = 240;                         // 5 WPM
        if (symbol_.size() < 8) symbol_ += ms > 2.0 * dotMs_ ? '-' : '.';
    }

    void flush() {
        if (symbol_.empty()) return;
        char found = 0;
        for (const char* p = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?/=+-"; *p; p++)
            if (symbol_ == morse::encode(*p)) { found = *p; break; }
        text += found ? found : '*';
        needSpace_ = true;                                      // a word gap after this letter becomes a space
        symbol_.clear();
    }

    float coeff_ = 0, s1_ = 0, s2_ = 0;
    int inBlock_ = 0, agree_ = 0;
    int warmup_ = 60;                                           // blocks (0.3 s)
    double peak_ = 0, noise_ = 0, durationMs_ = 0, dotMs_ = 60;
    bool pending_ = false, flushed_ = true, wordSent_ = true, needSpace_ = false;
    double recent_[8] = {0};
    int recentNext_ = 0, recentCount_ = 0;
    std::string symbol_;
};

// Audio for a text in Morse code (practice signal, tests).
inline std::vector<int16_t> synthesize(const std::string& text, int wpm, double toneHz = 700.0, int amplitude = 10000) {
    std::vector<int16_t> out;
    int dot = SAMPLE_RATE * morse::dotMs(wpm) / 1000;
    double phase = 0, step = 6.283185307179586 * toneHz / SAMPLE_RATE;
    auto add = [&](int units, bool on) {
        for (int i = 0; i < units * dot; i++) {
            out.push_back(on ? (int16_t)(amplitude * std::sin(phase)) : 0);
            phase += step;
        }
    };
    add(7, false);
    for (char c : text) {
        if (c == ' ') { add(4, false); continue; }              // 3 after the letter + 4 = 7
        for (const char* e = morse::encode(c); *e; e++) {
            add(*e == '-' ? 3 : 1, true);
            add(1, false);
        }
        add(2, false);
    }
    add(10, false);
    return out;
}

}  // namespace cw
