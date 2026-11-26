// PSK31 (BPSK, 31.25 baud, Varicode): transmit audio, receiver with
// automatic frequency and symbol tracking, and a spectrum for the waterfall.
// Pure C++, host-testable. The Varicode table is checked against fldigi's in
// tools/psk31/check_varicode.py.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace psk31 {

constexpr double kBaud = 31.25;

// Varicode for ASCII 0..127, as text of 1s and 0s (characters are separated by "00").
inline const char* varicode(int c) {
    static const char* const table[128] = {
        "1010101011", "1011011011", "1011101101", "1101110111", "1011101011", "1101011111", "1011101111", "1011111101",
        "1011111111", "11101111", "11101", "1101101111", "1011011101", "11111", "1101110101", "1110101011",
        "1011110111", "1011110101", "1110101101", "1110101111", "1101011011", "1101101011", "1101101101", "1101010111",
        "1101111011", "1101111101", "1110110111", "1101010101", "1101011101", "1110111011", "1011111011", "1101111111",
        "1", "111111111", "101011111", "111110101", "111011011", "1011010101", "1010111011", "101111111",
        "11111011", "11110111", "101101111", "111011111", "1110101", "110101", "1010111", "110101111",
        "10110111", "10111101", "11101101", "11111111", "101110111", "101011011", "101101011", "110101101",
        "110101011", "110110111", "11110101", "110111101", "111101101", "1010101", "111010111", "1010101111",
        "1010111101", "1111101", "11101011", "10101101", "10110101", "1110111", "11011011", "11111101",
        "101010101", "1111111", "111111101", "101111101", "11010111", "10111011", "11011101", "10101011",
        "11010101", "111011101", "10101111", "1101111", "1101101", "101010111", "110110101", "101011101",
        "101110101", "101111011", "1010101101", "111110111", "111101111", "111111011", "1010111111", "101101101",
        "1011011111", "1011", "1011111", "101111", "101101", "11", "111101", "1011011",
        "101011", "1101", "111101011", "10111111", "11011", "111011", "1111", "111",
        "111111", "110111111", "10101", "10111", "101", "110111", "1111011", "1101011",
        "11011111", "1011101", "111010101", "1010110111", "110111011", "1010110101", "1011010111", "1110110101",
    };
    return c >= 0 && c < 128 ? table[c] : nullptr;
}

// Bits for a text: every character's code followed by "00".
inline std::string encodeBits(const std::string& text) {
    std::string bits;
    for (char ch : text) {
        const char* v = varicode((unsigned char)ch);
        if (!v) continue;
        bits += v;
        bits += "00";
    }
    return bits;
}

// --------------------------------------------------------------- transmit ----

// Audio for a text: a preamble of phase reversals, the text, a postamble of
// steady carrier. A reversal ('0') fades through zero (cosine shaping), which
// keeps the signal narrow.
class Modulator {
public:
    Modulator(const std::string& text, double carrierHz, int sampleRate, int amplitude = 12000)
        : rate_(sampleRate), hz_(carrierHz), amplitude_(amplitude) {
        bits_ = std::string(32, '0') + encodeBits(text) + std::string(24, '1');
        perSymbol_ = (double)sampleRate / kBaud;
    }

    size_t totalSamples() const { return (size_t)(bits_.size() * perSymbol_) + (size_t)perSymbol_; }

    size_t read(int16_t* out, size_t max) {
        size_t n = 0;
        size_t total = totalSamples();
        while (n < max && pos_ < total) {
            size_t symbol = (size_t)(pos_ / perSymbol_);
            double t = (pos_ - symbol * perSymbol_) / perSymbol_;          // 0..1 within the symbol
            double env;
            if (symbol >= bits_.size()) env = 0.5 * (1 + std::cos(M_PI * t)) * sign_;          // fade out
            else if (bits_[symbol] == '0') env = std::cos(M_PI * t) * sign_;                   // reversal
            else env = sign_;
            if (symbol == 0) env *= 0.5 * (1 - std::cos(M_PI * t));                            // fade in
            out[n++] = (int16_t)(amplitude_ * env * std::sin(phase_));
            phase_ += 2 * M_PI * hz_ / rate_;
            if (phase_ > 2 * M_PI) phase_ -= 2 * M_PI;
            pos_++;
            if ((size_t)(pos_ / perSymbol_) != symbol && symbol < bits_.size() && bits_[symbol] == '0') sign_ = -sign_;
        }
        return n;
    }

private:
    std::string bits_;
    double rate_, hz_, perSymbol_, phase_ = 0;
    int amplitude_;
    double sign_ = 1;
    size_t pos_ = 0;
};

// ---------------------------------------------------------------- receive ----

class Demodulator {
public:
    std::string text;           // decoded characters since the caller last cleared it
    float quality = 0;          // about 1 when the phase steps are cleanly 0 or 180 degrees, about 0.5 in noise
    bool afc = true;

    explicit Demodulator(int sampleRate = 8000) : rate_(sampleRate) {
        decStep_ = 500.0 / sampleRate;                  // to 500 Hz: 16 samples per symbol (any input rate)
        // Low-pass before decimating (cut-off 100 Hz).
        const int taps = kTaps;
        double sum = 0;
        for (int i = 0; i < taps; i++) {
            double t = i - (taps - 1) / 2.0, fc = 100.0 / sampleRate;
            double h = t == 0 ? 2 * fc : std::sin(2 * M_PI * fc * t) / (M_PI * t);
            h *= 0.42 - 0.5 * std::cos(2 * M_PI * i / (taps - 1)) + 0.08 * std::cos(4 * M_PI * i / (taps - 1));
            lp_[i] = (float)h;
            sum += h;
        }
        for (int i = 0; i < taps; i++) lp_[i] /= (float)sum;
        // Matched filter at 500 Hz: one symbol long, raised cosine.
        for (int i = 0; i < kMatched; i++) mf_[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * (i + 0.5) / kMatched));
        setFrequency(1000);
    }

    void setFrequency(double hz) {
        hz_ = hz;
        for (float& e : energy_) e = 0;
        lagAccI_ = lagAccQ_ = magAcc_ = afcAccI_ = afcAccQ_ = 0;
        warmup_ = 10;
    }
    double frequency() const { return hz_; }

    void feed(const int16_t* samples, size_t n) {
        for (size_t k = 0; k < n; k++) {
            float s = samples[k] / 32768.0f;
            hi_[hpos_] = s * (float)std::cos(nco_);
            hq_[hpos_] = -s * (float)std::sin(nco_);
            hpos_ = (hpos_ + 1) % kTaps;
            nco_ += 2 * M_PI * hz_ / rate_;
            if (nco_ > 2 * M_PI) nco_ -= 2 * M_PI;
            decAcc_ += decStep_;
            if (decAcc_ < 1.0) continue;
            decAcc_ -= 1.0;
            float i = 0, q = 0;
            for (int t = 0; t < kTaps; t++) {
                int idx = (hpos_ + t) % kTaps;
                i += lp_[t] * hi_[idx];
                q += lp_[t] * hq_[idx];
            }
            atRate500(i, q);
        }
    }

private:
    static constexpr int kTaps = 129, kMatched = 16, kSps = 16, kLag = 2 * kSps;

    void atRate500(float i, float q) {
        mi_[mpos_] = i;
        mq_[mpos_] = q;
        mpos_ = (mpos_ + 1) % kMatched;
        float fi = 0, fq = 0;
        for (int t = 0; t < kMatched; t++) {
            int idx = (mpos_ + t) % kMatched;
            fi += mf_[t] * mi_[idx];
            fq += mf_[t] * mq_[idx];
        }
        // AFC: squaring takes out the 180 degree steps; what is left turns at
        // twice the tuning error. Measured sample to sample (500 Hz) it pulls in
        // errors of tens of Hz; the zero-amplitude moments of reversals weigh nothing.
        float wi = fi * fi - fq * fq, wq = 2 * fi * fq;
        afcI_ += wi * pwi_ + wq * pwq_;
        afcQ_ += wq * pwi_ - wi * pwq_;
        pwi_ = wi;
        pwq_ = wq;
        // Is it a carrier? Compared two symbols apart, a carrier still lines up
        // (whatever its offset) while noise through the 100 Hz filter does not.
        float li = lagI_[lagPos_], lq = lagQ_[lagPos_];
        lagSumI_ += wi * li + wq * lq;
        lagSumQ_ += wq * li - wi * lq;
        afcMag_ += std::sqrt(wi * wi + wq * wq) * std::sqrt(li * li + lq * lq);
        lagI_[lagPos_] = wi;
        lagQ_[lagPos_] = wq;
        lagPos_ = (lagPos_ + 1) % kLag;
        // Symbol timing: the matched filter output is strongest at the middle of a
        // symbol; keep a slowly averaged amplitude for each of the 16 phases.
        float amp = std::sqrt(fi * fi + fq * fq);
        energy_[phase_] = 0.97f * energy_[phase_] + 0.03f * amp;
        if (phase_ == best_) symbol(fi, fq);
        phase_ = (phase_ + 1) % kSps;
        if (phase_ == 0) {                              // once per symbol
            // Follow only a steady carrier: in noise the steps point every which way
            // and their sum is small next to the sum of their sizes.
            // Within one symbol the noise samples are not independent, so the
            // sums are carried over about ten symbols before comparing.
            lagAccI_ = 0.9f * lagAccI_ + lagSumI_;
            lagAccQ_ = 0.9f * lagAccQ_ + lagSumQ_;
            magAcc_ = 0.9f * magAcc_ + afcMag_;
            coherence_ = magAcc_ > 0 ? std::sqrt(lagAccI_ * lagAccI_ + lagAccQ_ * lagAccQ_) / magAcc_ : 0;
            // The frequency step is averaged the same way, and limited per symbol, so a
            // few symbols of noise after a signal ends cannot pull the tuning away.
            afcAccI_ = 0.8f * afcAccI_ + afcI_;
            afcAccQ_ = 0.8f * afcAccQ_ + afcQ_;
            if (warmup_ > 0) warmup_--;                 // let the sums fill first
            else if (afc && coherence_ > 0.55f) {
                double step = 0.15 * std::atan2(afcAccQ_, afcAccI_) / (2 * M_PI) * 500.0 / 2;
                hz_ += std::max(-2.0, std::min(2.0, step));
            }
            afcI_ = afcQ_ = afcMag_ = lagSumI_ = lagSumQ_ = 0;
            // re-pick the best sampling phase
            int b = 0;
            for (int p = 1; p < kSps; p++)
                if (energy_[p] > energy_[b]) b = p;
            // move one step at a time so the timing does not jump about in noise
            if (b != best_) best_ = (best_ + ((b - best_ + kSps) % kSps < kSps / 2 ? 1 : kSps - 1)) % kSps;
        }
    }

    void symbol(float i, float q) {
        // Phase step from the last symbol: about 0 = bit 1, about 180 degrees = bit 0.
        float dot = i * pi_ + q * pq_, cross = q * pi_ - i * pq_;
        float mag = std::sqrt(dot * dot + cross * cross);
        pi_ = i;
        pq_ = q;
        if (mag < 1e-12f) return;
        float c = dot / mag;
        // About 1 for a clean signal, about 0.5 for noise.
        quality = 0.9f * quality + 0.1f * c * c;
        pushBit(c > 0 ? 1 : 0);
    }

    void pushBit(int b) {      // (not "bit": Arduino defines a macro with that name)
        shift_ = (shift_ << 1) | (uint32_t)b;
        if ((shift_ & 3) == 0) {                        // "00": the end of a character
            uint32_t code = shift_ >> 2;
            if (code && quality > 0.62f && coherence_ > 0.4f) {        // squelch: a clean signal that is a carrier
                int len = 0;
                while (len < 32 && (code >> len)) len++;
                char buf[16];
                if (len <= 12) {
                    for (int k = 0; k < len; k++) buf[k] = (char)('0' + ((code >> (len - 1 - k)) & 1));
                    buf[len] = 0;
                    for (int ch = 0; ch < 128; ch++)
                        if (std::strcmp(varicode(ch), buf) == 0) {
                            if (ch == 10 || ch == 13) { if (text.empty() || text.back() != '\n') text += '\n'; }
                            else if (ch >= 32 && ch < 127) text += (char)ch;
                            break;
                        }
                }
            }
            shift_ = 0;
        } else if (shift_ > (1u << 14)) {
            shift_ &= 0x3FFF;                           // too long to be a character
        }
    }

    int rate_;
    double decStep_ = 1.0 / 16, decAcc_ = 0;
    double hz_ = 1000, nco_ = 0;
    float lp_[kTaps];
    float hi_[kTaps] = {0}, hq_[kTaps] = {0};
    int hpos_ = 0;
    float mf_[kMatched], mi_[kMatched] = {0}, mq_[kMatched] = {0};
    int mpos_ = 0;
    float energy_[kSps] = {0};
    int phase_ = 0, best_ = 0;
    float pi_ = 0, pq_ = 0;
    float pwi_ = 0, pwq_ = 0, afcI_ = 0, afcQ_ = 0, afcMag_ = 0, coherence_ = 0;
    float lagI_[kLag] = {0}, lagQ_[kLag] = {0}, lagSumI_ = 0, lagSumQ_ = 0, lagAccI_ = 0, lagAccQ_ = 0, magAcc_ = 0;
    float afcAccI_ = 0, afcAccQ_ = 0;
    int lagPos_ = 0, warmup_ = 10;
    uint32_t shift_ = 0;
};

// ------------------------------------------------------------- waterfall ----

// Power spectrum of the last 1024 samples (7.8 Hz per bin at 8 kHz), in dB.
class Spectrum {
public:
    static constexpr int N = 1024;
    explicit Spectrum(int sampleRate = 8000) : rate_(sampleRate) {
        for (int i = 0; i < N / 2; i++) {
            cos_[i] = (float)std::cos(2 * M_PI * i / N);
            sin_[i] = (float)std::sin(2 * M_PI * i / N);
        }
    }
    double hzPerBin() const { return (double)rate_ / N; }

    // Returns true when a new spectrum is ready (every `hop` samples).
    bool feed(int16_t s, int hop = 512) {
        buf_[pos_] = s;
        pos_ = (pos_ + 1) % N;
        if (++count_ < hop) return false;
        count_ = 0;
        compute();
        return true;
    }

    float db[N / 2];

private:
    void compute() {
        static float re[N], im[N];
        for (int i = 0; i < N; i++) {
            float w = 0.5f - 0.5f * (i < N / 2 ? cos_[i] : -cos_[i - N / 2]);
            re[i] = w * buf_[(pos_ + i) % N] / 32768.0f;
            im[i] = 0;
        }
        for (int i = 1, j = 0; i < N; i++) {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= N; len <<= 1)
            for (int start = 0; start < N; start += len)
                for (int k = 0; k < len / 2; k++) {
                    float wr = cos_[k * (N / len)], wi = -sin_[k * (N / len)];
                    int a = start + k, b = a + len / 2;
                    float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr; im[a] += xi;
                }
        for (int i = 0; i < N / 2; i++) db[i] = 10 * std::log10(1e-12f + re[i] * re[i] + im[i] * im[i]);
    }

    int rate_, pos_ = 0, count_ = 0;
    int16_t buf_[N] = {0};
    float cos_[N / 2], sin_[N / 2];
};

}  // namespace psk31
