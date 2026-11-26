// FT4: the same 77-bit messages and (174,91) code as FT8, sent in 7.5 second
// periods as 103 symbols of 4-FSK (20.833 Hz apart, 48 ms each) with four
// 4-symbol sync blocks. The message bits are scrambled with a fixed pattern
// before coding. Pure C++, host-testable; checked against ft8lib
// (tools/ft8/crosscheck_ft4.py).
//
// The receiver works at 32000/3 Hz (one symbol = 512 samples, one FFT bin =
// one tone spacing); Resampler converts from the microphone's 8000 Hz.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "Ft8.h"
#include "Ft8Decoder.h"

namespace ft4 {

constexpr int kSymbols = 103, kDataSymbols = 87;
constexpr float kToneSpacingHz = 12000.0f / 576.0f;            // 20.833 Hz
constexpr float kSymbolSeconds = 576.0f / 12000.0f;            // 0.048 s
constexpr int kPeriodMs = 7500;
constexpr float kNominalStartSeconds = 0.5f;
constexpr int kCostas[4][4] = {{0, 1, 3, 2}, {1, 0, 2, 3}, {2, 3, 1, 0}, {3, 2, 0, 1}};
constexpr int kGray[4] = {0, 1, 3, 2};
constexpr uint8_t kScramble[77] = {0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0,
                                   1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1,
                                   1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 1};

inline void scramble(const uint8_t in[10], uint8_t out[10]) {
    memcpy(out, in, 10);
    for (int i = 0; i < 77; i++)
        if (kScramble[i]) out[i >> 3] ^= (uint8_t)(0x80 >> (i & 7));
}

// Payload -> the 103 tones (0..3).
inline void tones(const uint8_t payload[10], uint8_t out[kSymbols]) {
    uint8_t s[10], bits[ft8::kCodeBits];
    scramble(payload, s);
    ft8::codeword(s, bits);
    int bit = 0;
    for (int i = 0; i < kSymbols; i++) {
        if (i < 4) out[i] = (uint8_t)kCostas[0][i];
        else if (i >= 33 && i < 37) out[i] = (uint8_t)kCostas[1][i - 33];
        else if (i >= 66 && i < 70) out[i] = (uint8_t)kCostas[2][i - 66];
        else if (i >= 99) out[i] = (uint8_t)kCostas[3][i - 99];
        else {
            out[i] = (uint8_t)kGray[(bits[bit] << 1) | bits[bit + 1]];
            bit += 2;
        }
    }
}

// Transmit audio: 4-tone FSK with Gaussian-smoothed transitions (BT = 1) and
// a one-symbol ramp at each end, as WSJT-X sends it.
class Synth {
public:
    Synth(const uint8_t toneList[kSymbols], float baseHz, int sampleRate, int amplitude = 12000, float leadSilenceSeconds = 0)
        : baseHz_(baseHz), rate_((float)sampleRate), amplitude_((float)amplitude) {
        memcpy(tones_, toneList, kSymbols);
        perSymbol_ = sampleRate * kSymbolSeconds;
        total_ = (size_t)(perSymbol_ * (kSymbols + 2));          // ramp symbol before and after
        silence_ = (size_t)(leadSilenceSeconds * sampleRate);
    }
    size_t totalSamples() const { return silence_ + total_; }

    size_t read(int16_t* out, size_t max) {
        size_t n = 0;
        while (n < max && silence_ > 0) { out[n++] = 0; silence_--; }
        const float twoPi = 6.2831853f;
        const float c = 3.14159265f * std::sqrt(2.0f / std::log(2.0f)) * 1.0f;     // BT = 1
        while (n < max && pos_ < total_) {
            float t = (float)pos_ / perSymbol_ - 1.0f;          // symbol time; -1..0 is the ramp-up symbol
            int k = (int)std::floor(t);
            float x = t - (float)k - 0.5f;
            float tone = 0;
            for (int d = -2; d <= 2; d++) {
                int j = k + d;
                float tj = (float)tones_[j < 0 ? 0 : j >= kSymbols ? kSymbols - 1 : j];
                float u = x - (float)d;
                tone += tj * 0.5f * (std::erf(c * (u + 0.5f)) - std::erf(c * (u - 0.5f)));
            }
            float envelope = 1.0f;
            if (t < 0) envelope = 0.5f * (1.0f - std::cos(3.14159265f * (t + 1.0f)));
            else if (t > kSymbols) envelope = 0.5f * (1.0f + std::cos(3.14159265f * (t - kSymbols)));
            out[n++] = (int16_t)(amplitude_ * envelope * std::sin(phase_));
            phase_ += twoPi * (baseHz_ + tone * kToneSpacingHz) / rate_;
            if (phase_ > twoPi) phase_ -= twoPi;
            pos_++;
        }
        return n;
    }

private:
    uint8_t tones_[kSymbols];
    float baseHz_, rate_, amplitude_, phase_ = 0, perSymbol_ = 0;
    size_t total_ = 0, pos_ = 0, silence_ = 0;
};

// ---------------------------------------------------------- receiving ----

constexpr float kRxRate = 32000.0f / 3.0f;     // 10666.67 Hz
constexpr int kSymbolSamples = 512;
constexpr int kTimeOsr = 2;
constexpr int kBlocks = 140;                   // symbols kept: 6.72 s of the period
constexpr float kMinHz = 200.0f, kMaxHz = 3000.0f;
constexpr int kMinBin = (int)(kMinHz / kToneSpacingHz);
constexpr int kBins = (int)((kMaxHz - kMinHz) / kToneSpacingHz);

// 8000 Hz -> 10666.67 Hz (4 output samples per 3 input).
class Resampler {
public:
    Resampler() {
        const double pi = 3.14159265358979323846, cutoff = 3400.0 / 8000.0;
        for (int phase = 0; phase < 4; phase++)
            for (int k = 0; k < kTaps; k++) {
                double t = (k - kHalf) - phase * 0.75;
                double sinc = std::fabs(t) < 1e-9 ? 2 * cutoff : std::sin(2 * pi * cutoff * t) / (pi * t);
                double window = 0.5 + 0.5 * std::cos(pi * t / (kHalf + 1));
                taps_[phase][k] = (float)(sinc * (std::fabs(t) <= kHalf + 1 ? window : 0.0));
            }
    }

    size_t process(const int16_t* in, size_t n, int16_t* out) {
        size_t produced = 0;
        for (size_t i = 0; i < n; i++) {
            history_[head_] = in[i];
            head_ = (head_ + 1) % kTaps;
            received_++;
            // Output m sits at input position 0.75 * m.
            while ((nextOut_ * 3) / 4 + kHalf < received_) {
                int phase = (int)((nextOut_ * 3) % 4);
                uint64_t centre = (nextOut_ * 3) / 4;
                float acc = 0;
                for (int k = 0; k < kTaps; k++) {
                    int64_t index = (int64_t)centre + (k - kHalf);
                    int64_t age = (int64_t)received_ - 1 - index;
                    if (age < 0 || age >= kTaps) continue;
                    acc += taps_[phase][k] * history_[(head_ + kTaps - 1 - (int)age) % kTaps];
                }
                out[produced++] = (int16_t)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
                nextOut_++;
            }
        }
        return produced;
    }

private:
    static constexpr int kHalf = 24, kTaps = 2 * kHalf + 1;
    float taps_[4][kTaps];
    int16_t history_[kTaps] = {0};
    int head_ = 0;
    uint64_t received_ = 0, nextOut_ = 0;
};

class Decoder {
public:
    static size_t memoryNeeded(int freqOsr) {
        size_t nfft = (size_t)kSymbolSamples * (size_t)freqOsr;
        return (size_t)kRows * (size_t)freqOsr * kBins + nfft * (2 + 4 + 4) + sizeof(Decoder);
    }

    bool begin(int freqOsr) {
        end();
        freqOsr_ = freqOsr == 1 ? 1 : 2;
        nfft_ = kSymbolSamples * freqOsr_;
        bool ok = true;
        for (int r = 0; r < kRows; r++) {
            rows_[r] = (uint8_t*)malloc((size_t)freqOsr_ * kBins);
            ok = ok && rows_[r];
        }
        frame_ = (int16_t*)malloc(sizeof(int16_t) * (size_t)nfft_);
        re_ = (float*)malloc(sizeof(float) * (size_t)nfft_);
        im_ = (float*)malloc(sizeof(float) * (size_t)nfft_);
        if (!ok || !frame_ || !re_ || !im_) {
            end();
            return false;
        }
        fft_.init(nfft_);
        int count[ft8::kCodeBits] = {0};
        for (int m = 0; m < ft8::kParityBits; m++)
            for (int k = 0; k < 7; k++)
                if (ft8::kChecks[m][k]) {
                    int n = ft8::kChecks[m][k] - 1;
                    bitChecks_[n][count[n]++] = (uint8_t)m;
                }
        reset();
        return true;
    }

    void end() {
        for (int r = 0; r < kRows; r++) { free(rows_[r]); rows_[r] = nullptr; }
        free(frame_); free(re_); free(im_);
        frame_ = nullptr; re_ = im_ = nullptr;
    }

    Decoder() {}
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    ~Decoder() { end(); }
    bool ready() const { return frame_ != nullptr; }
    int freqOsr() const { return freqOsr_; }

    void reset() {
        if (!ready()) return;
        for (int r = 0; r < kRows; r++) memset(rows_[r], 0, (size_t)freqOsr_ * kBins);
        memset(frame_, 0, sizeof(int16_t) * (size_t)nfft_);
        steps_ = pending_ = 0;
        candidates_.clear();
        next_ = 0;
        found_.clear();
    }

    bool full() const { return steps_ >= kRows; }

    // Audio at kRxRate.
    void feed(const int16_t* samples, size_t n) {
        const int hop = kSymbolSamples / kTimeOsr;
        for (size_t i = 0; i < n && !full(); i++) {
            hopBuf_[pending_++] = samples[i];
            if (pending_ < hop) continue;
            pending_ = 0;
            memmove(frame_, frame_ + hop, sizeof(int16_t) * (size_t)(nfft_ - hop));
            memcpy(frame_ + (nfft_ - hop), hopBuf_, sizeof(int16_t) * (size_t)hop);
            spectrum();
            steps_++;
        }
    }

    void startDecode() {
        findCandidates();
        next_ = 0;
    }

    bool step(std::vector<ft8::Decoded>& out, int perCall = 2, const std::string& myCall = "") {
        for (int k = 0; k < perCall && next_ < candidates_.size(); k++) {
            ft8::Decoded d;
            if (tryCandidate(candidates_[next_++], d, myCall)) out.push_back(d);
        }
        return next_ < candidates_.size();
    }

private:
    struct Candidate {
        int16_t score, timeOffset, freqOffset;
        uint8_t timeSub, freqSub;
    };
    static constexpr int kRows = kBlocks * kTimeOsr;

    const uint8_t* cell(int block, int timeSub, int freqSub) const {
        return rows_[block * kTimeOsr + timeSub] + (size_t)freqSub * kBins;
    }

    void spectrum() {
        const float norm = 2.0f / (float)nfft_ / 32768.0f;
        for (int i = 0; i < nfft_; i++) {
            float c = i < nfft_ / 2 ? fft_.cosine(i) : -fft_.cosine(i - nfft_ / 2);
            re_[i] = norm * (0.5f - 0.5f * c) * (float)frame_[i];
            im_[i] = 0;
        }
        fft_.run(re_, im_);
        uint8_t* row = rows_[steps_];
        for (int sub = 0; sub < freqOsr_; sub++)
            for (int bin = 0; bin < kBins; bin++) {
                int src = (kMinBin + bin) * freqOsr_ + sub;
                float power = re_[src] * re_[src] + im_[src] * im_[src];
                int scaled = (int)(2.0f * 10.0f * std::log10(1e-12f + power) + 240.0f);
                row[(size_t)sub * kBins + (size_t)bin] = (uint8_t)(scaled < 0 ? 0 : scaled > 255 ? 255 : scaled);
            }
    }

    int syncScore(int timeOffset, int timeSub, int freqOffset, int freqSub) const {
        static const int starts[4] = {0, 33, 66, 99};
        int score = 0, terms = 0;
        for (int m = 0; m < 4; m++)
            for (int k = 0; k < 4; k++) {
                int block = timeOffset + starts[m] + k;
                if (block < 0) continue;
                if (block >= kBlocks) break;
                const uint8_t* p = cell(block, timeSub, freqSub) + freqOffset;
                int tone = kCostas[m][k];
                if (tone > 0) { score += p[tone] - p[tone - 1]; terms++; }
                if (tone < 3) { score += p[tone] - p[tone + 1]; terms++; }
                if (k > 0 && block > 0) { score += p[tone] - cell(block - 1, timeSub, freqSub)[freqOffset + tone]; terms++; }
                if (k < 3 && block + 1 < kBlocks) { score += p[tone] - cell(block + 1, timeSub, freqSub)[freqOffset + tone]; terms++; }
            }
        return terms ? score / terms : 0;
    }

    void findCandidates() {
        candidates_.clear();
        const int minScore = 8;
        const size_t maxCandidates = 60;
        for (int timeSub = 0; timeSub < kTimeOsr; timeSub++)
            for (int freqSub = 0; freqSub < freqOsr_; freqSub++)
                for (int timeOffset = -6; timeOffset <= kBlocks - kSymbols + 4; timeOffset++)
                    for (int freqOffset = 0; freqOffset + 3 < kBins; freqOffset++) {
                        int score = syncScore(timeOffset, timeSub, freqOffset, freqSub);
                        if (score < minScore) continue;
                        Candidate c = {(int16_t)score, (int16_t)timeOffset, (int16_t)freqOffset, (uint8_t)timeSub, (uint8_t)freqSub};
                        if (candidates_.size() < maxCandidates) {
                            candidates_.push_back(c);
                        } else {
                            size_t weakest = 0;
                            for (size_t i = 1; i < candidates_.size(); i++)
                                if (candidates_[i].score < candidates_[weakest].score) weakest = i;
                            if (candidates_[weakest].score < score) candidates_[weakest] = c;
                        }
                    }
        std::sort(candidates_.begin(), candidates_.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    }

    static float fastTanh(float x) {
        if (x < -4.97f) return -1.0f;
        if (x > 4.97f) return 1.0f;
        float x2 = x * x;
        return x * (945.0f + x2 * (105.0f + x2)) / (945.0f + x2 * (420.0f + x2 * 15.0f));
    }
    static float fastAtanh(float x) {
        float x2 = x * x;
        return x * (945.0f + x2 * (-735.0f + x2 * 64.0f)) / (945.0f + x2 * (-1050.0f + x2 * 225.0f));
    }

    void softBits(const Candidate& c) {
        for (int s = 0; s < kDataSymbols; s++) {
            int symbol = s + 4 + (s / 29) * 4;              // skip the sync blocks
            int block = c.timeOffset + symbol;
            float* l = llr_ + 2 * s;
            if (block < 0 || block >= kBlocks) {
                l[0] = l[1] = 0;
                continue;
            }
            const uint8_t* p = cell(block, c.timeSub, c.freqSub) + c.freqOffset;
            float v[4];
            for (int j = 0; j < 4; j++) v[j] = (float)p[kGray[j]];
            l[0] = std::max(v[2], v[3]) - std::max(v[0], v[1]);
            l[1] = std::max(v[1], v[3]) - std::max(v[0], v[2]);
        }
        float sum = 0, sum2 = 0;
        for (int i = 0; i < ft8::kCodeBits; i++) { sum += llr_[i]; sum2 += llr_[i] * llr_[i]; }
        float variance = (sum2 - sum * sum / ft8::kCodeBits) / ft8::kCodeBits;
        float scale = variance > 0 ? std::sqrt(24.0f / variance) : 0;
        for (int i = 0; i < ft8::kCodeBits; i++) llr_[i] *= scale;
    }

    int ldpc(uint8_t bits[ft8::kCodeBits]) {
        using ft8::kChecks;
        memset(toBit_, 0, sizeof(toBit_));
        int errors = ft8::kParityBits;
        for (int iteration = 0; iteration < 20; iteration++) {
            for (int n = 0; n < ft8::kCodeBits; n++) bits[n] = (llr_[n] + toBit_[n][0] + toBit_[n][1] + toBit_[n][2]) > 0 ? 1 : 0;
            errors = 0;
            for (int m = 0; m < ft8::kParityBits; m++) {
                int parity = 0;
                for (int k = 0; k < 7; k++)
                    if (kChecks[m][k]) parity ^= bits[kChecks[m][k] - 1];
                errors += parity;
            }
            if (errors == 0) break;
            for (int m = 0; m < ft8::kParityBits; m++)
                for (int k = 0; k < 7; k++) {
                    if (!kChecks[m][k]) continue;
                    int n = kChecks[m][k] - 1;
                    float t = llr_[n];
                    for (int j = 0; j < 3; j++)
                        if (bitChecks_[n][j] != m) t += toBit_[n][j];
                    toCheck_[m][k] = fastTanh(-t / 2);
                }
            for (int n = 0; n < ft8::kCodeBits; n++)
                for (int j = 0; j < 3; j++) {
                    int m = bitChecks_[n][j];
                    float t = 1.0f;
                    for (int k = 0; k < 7; k++)
                        if (kChecks[m][k] && kChecks[m][k] - 1 != n) t *= toCheck_[m][k];
                    toBit_[n][j] = -2.0f * fastAtanh(t);
                }
        }
        return errors;
    }

    bool tryCandidate(const Candidate& c, ft8::Decoded& out, const std::string& myCall) {
        softBits(c);
        uint8_t bits[ft8::kCodeBits];
        if (ldpc(bits) != 0) return false;
        uint8_t scrambled[10] = {0};
        bool any = false;
        for (int i = 0; i < 77; i++)
            if (bits[i]) { scrambled[i >> 3] |= (uint8_t)(0x80 >> (i & 7)); any = true; }
        if (!any) return false;
        int crc = 0;
        for (int i = 0; i < 14; i++) crc = (crc << 1) | bits[77 + i];
        if (crc != ft8::crc14(scrambled)) return false;
        uint8_t payload[10];
        scramble(scrambled, payload);                       // the same XOR undoes it
        payload[9] &= 0xF8;
        for (const ft8::Decoded& d : found_)
            if (memcmp(d.payload, payload, 10) == 0) return false;
        out.text = ft8::unpack(payload, myCall);
        if (out.text.empty()) return false;
        memcpy(out.payload, payload, 10);
        out.freqHz = (kMinBin + c.freqOffset + (float)c.freqSub / (float)freqOsr_) * kToneSpacingHz;
        out.dt = (c.timeOffset + (float)c.timeSub / kTimeOsr) * kSymbolSeconds - kNominalStartSeconds;
        uint8_t sent[kSymbols];
        tones(payload, sent);
        float signal = 0, noise = 0;
        int used = 0;
        for (int s = 0; s < kSymbols; s++) {
            int block = c.timeOffset + s;
            if (block < 0 || block >= kBlocks) continue;
            const uint8_t* p = cell(block, c.timeSub, c.freqSub) + c.freqOffset;
            float others = 0;
            for (int t = 0; t < 4; t++)
                if (t != sent[s]) others += p[t];
            signal += p[sent[s]];
            noise += others / 3.0f;
            used++;
        }
        if (used) {
            // 0.5 dB units; one 20.8 Hz bin to the 2500 Hz reference bandwidth is -20.8 dB
            float db = (signal - noise) / used / 2.0f - 20.8f;
            out.snr = (int)std::lround(db < -24 ? -24 : db > 30 ? 30 : db);
        }
        found_.push_back(out);
        return true;
    }

    int freqOsr_ = 2, nfft_ = 0, steps_ = 0, pending_ = 0;
    uint8_t* rows_[kRows] = {nullptr};
    int16_t* frame_ = nullptr;
    float* re_ = nullptr;
    float* im_ = nullptr;
    int16_t hopBuf_[kSymbolSamples / kTimeOsr];
    ft8::Fft fft_;
    std::vector<Candidate> candidates_;
    size_t next_ = 0;
    std::vector<ft8::Decoded> found_;
    float llr_[ft8::kCodeBits];
    float toCheck_[ft8::kParityBits][7], toBit_[ft8::kCodeBits][3];
    uint8_t bitChecks_[ft8::kCodeBits][3];
};

}  // namespace ft4
