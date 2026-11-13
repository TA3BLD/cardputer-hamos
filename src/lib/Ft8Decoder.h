// FT8 receiver: spectrogram of one 15 second period, search for the
// synchronisation pattern, soft demodulation, LDPC decoding (belief
// propagation) and CRC check. Pure C++, host-testable.
//
// Input is audio at 6400 Hz (one symbol = 1024 samples); Resampler converts
// from the 8000 Hz the microphone delivers.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "Ft8.h"

namespace ft8 {

constexpr int kRxRate = 6400;               // samples per second into the decoder
constexpr int kSymbolSamples = 1024;        // at kRxRate
constexpr int kTimeOsr = 2;                 // spectra per symbol
constexpr int kBlocks = 88;                 // symbols kept: 14.08 s of the period
constexpr float kMinHz = 200.0f, kMaxHz = 3000.0f;
constexpr int kBins = (int)((kMaxHz - kMinHz) / kToneSpacingHz);       // 448 tone-spaced bins
constexpr int kMinBin = (int)(kMinHz / kToneSpacingHz);
constexpr float kNominalStartSeconds = 0.5f;                            // transmissions begin 0.5 s into the period

struct Decoded {
    std::string text;
    uint8_t payload[10];
    float freqHz = 0;       // audio frequency of the lowest tone
    float dt = 0;           // seconds early (-) or late (+)
    int snr = 0;            // dB in 2500 Hz, like WSJT-X reports (estimate)
};

// ---------------------------------------------------------- resampling ----

// 8000 Hz -> 6400 Hz (4 output samples per 5 input) with a low-pass filter.
class Resampler {
public:
    Resampler() {
        const double pi = 3.14159265358979323846, cutoff = 2950.0 / 8000.0;    // of the input rate
        for (int phase = 0; phase < 4; phase++)
            for (int k = 0; k < kTaps; k++) {
                double t = (k - kHalf) - phase * 0.25;          // distance from the output instant, in input samples
                double sinc = std::fabs(t) < 1e-9 ? 2 * cutoff : std::sin(2 * pi * cutoff * t) / (pi * t);
                double window = 0.5 + 0.5 * std::cos(pi * t / (kHalf + 1));
                taps_[phase][k] = (float)(sinc * (std::fabs(t) <= kHalf + 1 ? window : 0.0));
            }
    }

    // Returns the number of samples written to `out` (at most 4/5 of n, plus one).
    size_t process(const int16_t* in, size_t n, int16_t* out) {
        size_t produced = 0;
        for (size_t i = 0; i < n; i++) {
            history_[head_] = in[i];
            head_ = (head_ + 1) % kTaps;
            received_++;
            // Output m sits at input position 1.25 * m; it needs kHalf samples after that point.
            while ((nextOut_ * 5) / 4 + kHalf < received_) {
                int phase = (int)((nextOut_ * 5) % 4);
                uint64_t centre = (nextOut_ * 5) / 4;
                // history_[head_] is the oldest sample: input index received_ - kTaps
                float acc = 0;
                for (int k = 0; k < kTaps; k++) {
                    int64_t index = (int64_t)centre + (k - kHalf);
                    int64_t age = (int64_t)received_ - 1 - index;       // 0 = newest
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

// ----------------------------------------------------------------- FFT ----

// Plain radix-2 FFT, in place, single precision.
class Fft {
public:
    void init(int n) {
        n_ = n;
        cos_.resize((size_t)n / 2);
        sin_.resize((size_t)n / 2);
        for (int i = 0; i < n / 2; i++) {
            cos_[(size_t)i] = (float)std::cos(2 * 3.14159265358979323846 * i / n);
            sin_[(size_t)i] = (float)std::sin(2 * 3.14159265358979323846 * i / n);
        }
    }
    float cosine(int i) const { return cos_[(size_t)i]; }       // cos(2 pi i / n), i < n/2

    void run(float* re, float* im) const {
        for (int i = 1, j = 0; i < n_; i++) {                   // bit reversal
            int bit = n_ >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= n_; len <<= 1) {
            int step = n_ / len;
            for (int start = 0; start < n_; start += len)
                for (int k = 0; k < len / 2; k++) {
                    float wr = cos_[(size_t)(k * step)], wi = -sin_[(size_t)(k * step)];
                    int a = start + k, b = a + len / 2;
                    float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr; im[a] += xi;
                }
        }
    }

private:
    int n_ = 0;
    std::vector<float> cos_, sin_;
};

// ------------------------------------------------------------- decoder ----

class Decoder {
public:
    // freqOsr 2 resolves half a tone spacing (needs about 190 KB of RAM);
    // freqOsr 1 is a little less sensitive and needs about 100 KB.
    // Returns false if the memory is not available.
    bool begin(int freqOsr) {
        end();
        freqOsr_ = freqOsr == 1 ? 1 : 2;
        nfft_ = kSymbolSamples * freqOsr_;
        size_t cells = (size_t)kBlocks * kTimeOsr * (size_t)freqOsr_ * kBins;
        waterfall_ = (uint8_t*)malloc(cells);
        frame_ = (int16_t*)malloc(sizeof(int16_t) * (size_t)nfft_);
        re_ = (float*)malloc(sizeof(float) * (size_t)nfft_);
        im_ = (float*)malloc(sizeof(float) * (size_t)nfft_);
        if (!waterfall_ || !frame_ || !re_ || !im_) {
            end();
            return false;
        }
        fft_.init(nfft_);
        // Which three checks each codeword bit takes part in.
        int count[kCodeBits] = {0};
        for (int m = 0; m < kParityBits; m++)
            for (int k = 0; k < 7; k++)
                if (kChecks[m][k]) {
                    int n = kChecks[m][k] - 1;
                    bitChecks_[n][count[n]++] = (uint8_t)m;
                }
        reset();
        return true;
    }

    void end() {
        free(waterfall_); free(frame_); free(re_); free(im_);
        waterfall_ = nullptr; frame_ = nullptr; re_ = im_ = nullptr;
    }

    ~Decoder() { end(); }
    bool ready() const { return waterfall_ != nullptr; }
    int freqOsr() const { return freqOsr_; }

    // Start of a new period.
    void reset() {
        if (!ready()) return;
        memset(waterfall_, 0, (size_t)kBlocks * kTimeOsr * (size_t)freqOsr_ * kBins);
        memset(frame_, 0, sizeof(int16_t) * (size_t)nfft_);
        steps_ = 0;
        pending_ = 0;
        candidates_.clear();
        next_ = 0;
        found_.clear();
    }

    bool full() const { return steps_ >= kBlocks * kTimeOsr; }
    float progress() const { return (float)steps_ / (kBlocks * kTimeOsr); }

    // Audio at 6400 Hz. Samples beyond the kept part of the period are ignored.
    void feed(const int16_t* samples, size_t n) {
        const int hop = kSymbolSamples / kTimeOsr;
        for (size_t i = 0; i < n && !full(); i++) {
            // The analysis frame always holds the newest nfft samples.
            hopBuf_[pending_++] = samples[i];
            if (pending_ < hop) continue;
            pending_ = 0;
            memmove(frame_, frame_ + hop, sizeof(int16_t) * (size_t)(nfft_ - hop));
            memcpy(frame_ + (nfft_ - hop), hopBuf_, sizeof(int16_t) * (size_t)hop);
            spectrum();
            steps_++;
        }
    }

    // Decoding in slices: startDecode() finds the candidates, then each step()
    // tries a few of them and appends new messages to `out`. step() returns
    // false when everything has been tried.
    void startDecode() {
        findCandidates();
        next_ = 0;
    }

    bool step(std::vector<Decoded>& out, int perCall = 2, const std::string& myCall = "") {
        for (int k = 0; k < perCall && next_ < candidates_.size(); k++) {
            Decoded d;
            if (tryCandidate(candidates_[next_++], d, myCall)) out.push_back(d);
        }
        return next_ < candidates_.size();
    }

    size_t candidateCount() const { return candidates_.size(); }

private:
    struct Candidate {
        int16_t score, timeOffset, freqOffset;      // in symbols / tone spacings
        uint8_t timeSub, freqSub;
    };

    // One spectrogram value: 0.5 dB steps.
    const uint8_t* cell(int block, int timeSub, int freqSub) const {
        return waterfall_ + (((size_t)block * kTimeOsr + (size_t)timeSub) * (size_t)freqOsr_ + (size_t)freqSub) * kBins;
    }

    void spectrum() {
        const float norm = 2.0f / (float)nfft_ / 32768.0f;
        for (int i = 0; i < nfft_; i++) {
            // Hann window from the FFT's own cosine table
            float c = i < nfft_ / 2 ? fft_.cosine(i) : -fft_.cosine(i - nfft_ / 2);
            re_[i] = norm * (0.5f - 0.5f * c) * (float)frame_[i];
            im_[i] = 0;
        }
        fft_.run(re_, im_);
        uint8_t* row = waterfall_ + (size_t)steps_ * (size_t)freqOsr_ * kBins;
        for (int sub = 0; sub < freqOsr_; sub++)
            for (int bin = 0; bin < kBins; bin++) {
                int src = (kMinBin + bin) * freqOsr_ + sub;
                float power = re_[src] * re_[src] + im_[src] * im_[src];
                int scaled = (int)(2.0f * 10.0f * std::log10(1e-12f + power) + 240.0f);
                row[(size_t)sub * kBins + (size_t)bin] = (uint8_t)(scaled < 0 ? 0 : scaled > 255 ? 255 : scaled);
            }
    }

    // How well the three 7-symbol sync blocks stand out at this position:
    // each expected tone against its neighbours in frequency and in time.
    int syncScore(int timeOffset, int timeSub, int freqOffset, int freqSub) const {
        int score = 0, terms = 0;
        for (int m = 0; m < 3; m++)
            for (int k = 0; k < 7; k++) {
                int block = timeOffset + 36 * m + k;
                if (block < 0) continue;
                if (block >= kBlocks) break;
                const uint8_t* p = cell(block, timeSub, freqSub) + freqOffset;
                int tone = kCostas[k];
                if (tone > 0) { score += p[tone] - p[tone - 1]; terms++; }
                if (tone < 7) { score += p[tone] - p[tone + 1]; terms++; }
                if (k > 0 && block > 0) { score += p[tone] - cell(block - 1, timeSub, freqSub)[freqOffset + tone]; terms++; }
                if (k < 6 && block + 1 < kBlocks) { score += p[tone] - cell(block + 1, timeSub, freqSub)[freqOffset + tone]; terms++; }
            }
        return terms ? score / terms : 0;
    }

    void findCandidates() {
        candidates_.clear();
        const int minScore = 10;
        const size_t maxCandidates = 80;
        // Start between about 1 s early and 1 s late.
        for (int timeSub = 0; timeSub < kTimeOsr; timeSub++)
            for (int freqSub = 0; freqSub < freqOsr_; freqSub++)
                for (int timeOffset = -4; timeOffset <= kBlocks - kSymbols + 3; timeOffset++)
                    for (int freqOffset = 0; freqOffset + 7 < kBins; freqOffset++) {
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

    // Soft bits (positive = 1) for the 174 codeword bits of a candidate.
    void softBits(const Candidate& c) {
        for (int s = 0; s < 58; s++) {
            int symbol = s + (s < 29 ? 7 : 14);             // skip the sync blocks
            int block = c.timeOffset + symbol;
            float* l = llr_ + 3 * s;
            if (block < 0 || block >= kBlocks) {
                l[0] = l[1] = l[2] = 0;
                continue;
            }
            const uint8_t* p = cell(block, c.timeSub, c.freqSub) + c.freqOffset;
            float v[8];
            for (int j = 0; j < 8; j++) v[j] = (float)p[kGray[j]];      // strength of each 3-bit value
            l[0] = std::max(std::max(v[4], v[5]), std::max(v[6], v[7])) - std::max(std::max(v[0], v[1]), std::max(v[2], v[3]));
            l[1] = std::max(std::max(v[2], v[3]), std::max(v[6], v[7])) - std::max(std::max(v[0], v[1]), std::max(v[4], v[5]));
            l[2] = std::max(std::max(v[1], v[3]), std::max(v[5], v[7])) - std::max(std::max(v[0], v[2]), std::max(v[4], v[6]));
        }
        // Scale to the spread the decoder expects.
        float sum = 0, sum2 = 0;
        for (int i = 0; i < kCodeBits; i++) { sum += llr_[i]; sum2 += llr_[i] * llr_[i]; }
        float variance = (sum2 - sum * sum / kCodeBits) / kCodeBits;
        float scale = variance > 0 ? std::sqrt(24.0f / variance) : 0;
        for (int i = 0; i < kCodeBits; i++) llr_[i] *= scale;
    }

    // Sum-product decoding. Returns the number of failed parity checks (0 = a valid codeword).
    int ldpc(uint8_t bits[kCodeBits]) {
        memset(toBit_, 0, sizeof(toBit_));
        int errors = kParityBits;
        for (int iteration = 0; iteration < 20; iteration++) {
            for (int n = 0; n < kCodeBits; n++)
                bits[n] = (llr_[n] + toBit_[n][0] + toBit_[n][1] + toBit_[n][2]) > 0 ? 1 : 0;
            errors = 0;
            for (int m = 0; m < kParityBits; m++) {
                int parity = 0;
                for (int k = 0; k < 7; k++)
                    if (kChecks[m][k]) parity ^= bits[kChecks[m][k] - 1];
                errors += parity;
            }
            if (errors == 0) break;
            // bits -> checks
            for (int m = 0; m < kParityBits; m++)
                for (int k = 0; k < 7; k++) {
                    if (!kChecks[m][k]) continue;
                    int n = kChecks[m][k] - 1;
                    float t = llr_[n];
                    for (int j = 0; j < 3; j++)
                        if (bitChecks_[n][j] != m) t += toBit_[n][j];
                    toCheck_[m][k] = fastTanh(-t / 2);
                }
            // checks -> bits
            for (int n = 0; n < kCodeBits; n++)
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

    bool tryCandidate(const Candidate& c, Decoded& out, const std::string& myCall) {
        softBits(c);
        uint8_t bits[kCodeBits];
        if (ldpc(bits) != 0) return false;
        uint8_t payload[10] = {0};
        bool any = false;
        for (int i = 0; i < 77; i++) {
            if (bits[i]) { payload[i >> 3] |= (uint8_t)(0x80 >> (i & 7)); any = true; }
        }
        if (!any) return false;                             // the all-zero codeword is not a message
        int crc = 0;
        for (int i = 0; i < 14; i++) crc = (crc << 1) | bits[77 + i];
        if (crc != crc14(payload)) return false;
        for (const Decoded& d : found_)
            if (memcmp(d.payload, payload, 10) == 0) return false;      // same message found again
        out.text = unpack(payload, myCall);
        if (out.text.empty()) return false;
        memcpy(out.payload, payload, 10);
        out.freqHz = (kMinBin + c.freqOffset + (float)c.freqSub / (float)freqOsr_) * kToneSpacingHz;
        out.dt = (c.timeOffset + (float)c.timeSub / kTimeOsr) * kSymbolSeconds - kNominalStartSeconds;

        // Signal strength: the transmitted tones against the other seven tone slots.
        uint8_t sent[kSymbols];
        tones(payload, sent);
        float signal = 0, noise = 0;
        int used = 0;
        for (int s = 0; s < kSymbols; s++) {
            int block = c.timeOffset + s;
            if (block < 0 || block >= kBlocks) continue;
            const uint8_t* p = cell(block, c.timeSub, c.freqSub) + c.freqOffset;
            float others = 0;
            for (int t = 0; t < 8; t++)
                if (t != sent[s]) others += p[t];
            signal += p[sent[s]];
            noise += others / 7.0f;
            used++;
        }
        if (used) {
            // 0.5 dB units, then from one 6.25 Hz bin to the 2500 Hz reference
            // bandwidth (-26 dB) plus an offset measured with tools/ft8/crosscheck.py.
            float db = (signal - noise) / used / 2.0f - 26.0f - (freqOsr_ == 2 ? 0.1f : -2.1f);
            out.snr = (int)std::lround(db < -24 ? -24 : db > 30 ? 30 : db);
        }
        found_.push_back(out);
        return true;
    }

    int freqOsr_ = 2, nfft_ = 0, steps_ = 0, pending_ = 0;
    uint8_t* waterfall_ = nullptr;
    int16_t* frame_ = nullptr;
    float* re_ = nullptr;
    float* im_ = nullptr;
    int16_t hopBuf_[kSymbolSamples / kTimeOsr];
    Fft fft_;
    std::vector<Candidate> candidates_;
    size_t next_ = 0;
    std::vector<Decoded> found_;
    float llr_[kCodeBits];
    float toCheck_[kParityBits][7], toBit_[kCodeBits][3];
    uint8_t bitChecks_[kCodeBits][3];
};

}  // namespace ft8
