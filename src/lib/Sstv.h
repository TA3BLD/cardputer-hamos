// Slow-scan television: modes, transmit audio and a receiver.
// Pure C++, host-testable. Timings as in "Proposal for SSTV Mode
// Specifications" (JL Barber, Dayton 2000); checked against the Python
// packages pysstv and sstv (see tools/sstv/crosscheck.py).
//
// Frequencies: sync 1200 Hz, black 1500 Hz, white 2300 Hz.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sstv {

enum Kind { MARTIN, SCOTTIE, ROBOT36, ROBOT72, PD };

struct Mode {
    const char* name;
    uint8_t vis;
    uint16_t width, height;
    Kind kind;
    double syncMs, porchMs, sepMs, scanMs;      // scanMs: one colour (Martin, Scottie), Y (Robot), pixel x width (PD)
};

inline const Mode* modes(size_t& count) {
    static const Mode m[] = {
        {"Martin M1", 44, 320, 256, MARTIN, 4.862, 0.572, 0.572, 146.432},
        {"Martin M2", 40, 320, 256, MARTIN, 4.862, 0.572, 0.572, 73.216},
        {"Scottie S1", 60, 320, 256, SCOTTIE, 9.0, 1.5, 1.5, 138.240},
        {"Scottie S2", 56, 320, 256, SCOTTIE, 9.0, 1.5, 1.5, 88.064},
        {"Scottie DX", 76, 320, 256, SCOTTIE, 9.0, 1.5, 1.5, 345.600},
        {"Robot 36", 8, 320, 240, ROBOT36, 9.0, 3.0, 4.5, 88.0},
        {"Robot 72", 12, 320, 240, ROBOT72, 9.0, 3.0, 4.5, 138.0},
        {"PD 90", 99, 320, 256, PD, 20.0, 2.08, 0, 170.240},
        {"PD 120", 95, 640, 496, PD, 20.0, 2.08, 0, 121.600},
        {"PD 180", 96, 640, 496, PD, 20.0, 2.08, 0, 183.040},
    };
    count = sizeof(m) / sizeof(m[0]);
    return m;
}

inline const Mode* byVis(int vis) {
    size_t n;
    const Mode* m = modes(n);
    for (size_t i = 0; i < n; i++)
        if (m[i].vis == vis) return &m[i];
    return nullptr;
}

// Lines sent together in one go: PD and Robot 36 colour need two.
inline int linesPerUnit(const Mode& m) { return m.kind == PD ? 2 : 1; }

inline double unitMs(const Mode& m) {
    switch (m.kind) {
        case MARTIN: return m.syncMs + m.porchMs + 3 * m.scanMs + 3 * m.sepMs;
        case SCOTTIE: return 3 * m.sepMs + 3 * m.scanMs + m.syncMs;
        case ROBOT36: return m.syncMs + m.porchMs + m.scanMs + m.sepMs + 1.5 + m.scanMs / 2;
        case ROBOT72: return m.syncMs + m.porchMs + m.scanMs + 2 * (m.sepMs + 1.5 + m.scanMs / 2);
        case PD: return m.syncMs + m.porchMs + 4 * m.scanMs;
    }
    return 0;
}

// Where the sync pulse sits inside a unit (Scottie puts it before red).
inline double syncOffsetMs(const Mode& m) { return m.kind == SCOTTIE ? 2 * (m.sepMs + m.scanMs) : 0; }

inline double imageMs(const Mode& m) {
    return (m.kind == SCOTTIE ? m.syncMs : 0) + unitMs(m) * m.height / linesPerUnit(m);
}

constexpr double kVoxMs = 800, kVisMs = 300 + 10 + 300 + 30 * 10;

inline double transmissionMs(const Mode& m, bool vox) { return (vox ? kVoxMs : 0) + kVisMs + imageMs(m); }

inline double toneHz(int level) { return 1500.0 + 800.0 * level / 255.0; }       // 0..255 -> 1500..2300 Hz

// Full-range YCbCr (as JPEG and the reference encoders use).
inline void rgbToYcc(int r, int g, int b, int& y, int& cb, int& cr) {
    y = (int)std::lround(0.299 * r + 0.587 * g + 0.114 * b);
    cb = (int)std::lround(128 - 0.168736 * r - 0.331264 * g + 0.5 * b);
    cr = (int)std::lround(128 + 0.5 * r - 0.418688 * g - 0.081312 * b);
}

inline uint8_t clamp8(double v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : std::lround(v)); }

inline void yccToRgb(int y, int cb, int cr, uint8_t* rgb) {
    rgb[0] = clamp8(y + 1.402 * (cr - 128));
    rgb[1] = clamp8(y - 0.344136 * (cb - 128) - 0.714136 * (cr - 128));
    rgb[2] = clamp8(y + 1.772 * (cb - 128));
}

// ------------------------------------------------------------ transmit ----

// Supplies one image line: width x 3 bytes, R G B.
typedef bool (*LineSource)(void* user, int y, uint8_t* rgb);

class Encoder {
public:
    Encoder(const Mode& mode, LineSource source, void* user, int sampleRate, bool vox = true, int amplitude = 14000)
        : mode_(mode), source_(source), user_(user), rate_(sampleRate), amplitude_((float)amplitude),
          line_(mode.width * 3u), line2_(mode.width * 3u) {
        if (vox)
            for (int f : {1900, 1500, 1900, 1500, 2300, 1500, 2300, 1500}) add(f, 100);
        add(1900, 300);
        add(1200, 10);
        add(1900, 300);
        add(1200, 30);                                  // start bit
        int ones = 0;
        for (int i = 0; i < 7; i++) {
            int bit = (mode.vis >> i) & 1;
            ones += bit;
            add(bit ? 1100 : 1300, 30);
        }
        add(ones % 2 ? 1100 : 1300, 30);                // even parity
        add(1200, 30);                                  // stop bit
        if (mode.kind == SCOTTIE) add(1200, mode.syncMs);   // the starting sync
        total_ = (size_t)(transmissionMs(mode, vox) * rate_ / 1000.0);
    }

    // Writes up to `max` samples; returns 0 when the picture is complete.
    size_t read(int16_t* out, size_t max) {
        size_t n = 0;
        while (n < max) {
            if (seg_ >= segs_.size()) {
                segs_.clear();
                seg_ = 0;
                if (!nextUnit()) break;
                continue;
            }
            const Seg& s = segs_[seg_];
            if ((double)emitted_ >= s.endSample) {
                seg_++;
                continue;
            }
            out[n++] = (int16_t)(amplitude_ * std::sin(phase_));
            phase_ += 6.283185307179586 * s.hz / rate_;
            if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;
            emitted_++;
        }
        return n;
    }

    float progress() const { return total_ ? std::min(1.0f, (float)emitted_ / (float)total_) : 1.0f; }
    int currentLine() const { return y_; }

private:
    struct Seg {
        double hz, endSample;
    };

    void add(double hz, double ms) {
        clock_ += ms * rate_ / 1000.0;
        segs_.push_back({hz, clock_});
    }

    void pixels(const uint8_t* values, int stride, double ms) {
        double each = ms / mode_.width;
        for (int x = 0; x < mode_.width; x++) add(toneHz(values[x * stride]), each);
    }

    bool fetch(int y, std::vector<uint8_t>& into) {
        if (y >= mode_.height) return false;
        if (!source_(user_, y, into.data())) std::fill(into.begin(), into.end(), 0);
        return true;
    }

    // Queues the audio of the next line (or line pair).
    bool nextUnit() {
        if (y_ >= mode_.height) return false;
        const Mode& m = mode_;
        int w = m.width;
        fetch(y_, line_);
        std::vector<uint8_t> a(w), b(w), c(w);
        switch (m.kind) {
            case MARTIN:
                add(1200, m.syncMs);
                add(1500, m.porchMs);
                for (int ch : {1, 2, 0}) {              // green, blue, red
                    pixels(line_.data() + ch, 3, m.scanMs);
                    add(1500, m.sepMs);
                }
                break;
            case SCOTTIE:
                add(1500, m.sepMs);
                pixels(line_.data() + 1, 3, m.scanMs);
                add(1500, m.sepMs);
                pixels(line_.data() + 2, 3, m.scanMs);
                add(1200, m.syncMs);
                add(1500, m.sepMs);
                pixels(line_.data() + 0, 3, m.scanMs);
                break;
            case ROBOT36:
            case ROBOT72:
            case PD: {
                bool pd = m.kind == PD;
                if (pd) fetch(y_ + 1, line2_);
                std::vector<uint8_t> y0(w), y1(w), cb(w), cr(w);
                for (int x = 0; x < w; x++) {
                    int Y, Cb, Cr, Y2, Cb2, Cr2;
                    rgbToYcc(line_[x * 3], line_[x * 3 + 1], line_[x * 3 + 2], Y, Cb, Cr);
                    y0[x] = clamp8(Y);
                    if (pd) {
                        rgbToYcc(line2_[x * 3], line2_[x * 3 + 1], line2_[x * 3 + 2], Y2, Cb2, Cr2);
                        y1[x] = clamp8(Y2);
                        cb[x] = clamp8((Cb + Cb2) / 2.0);
                        cr[x] = clamp8((Cr + Cr2) / 2.0);
                    } else {
                        cb[x] = clamp8(Cb);
                        cr[x] = clamp8(Cr);
                    }
                }
                add(1200, m.syncMs);
                add(1500, m.porchMs);
                pixels(y0.data(), 1, m.scanMs);
                if (m.kind == ROBOT36) {
                    bool even = y_ % 2 == 0;            // even lines carry R-Y, odd lines B-Y
                    add(even ? 1500 : 2300, m.sepMs);
                    add(1900, 1.5);
                    pixels(even ? cr.data() : cb.data(), 1, m.scanMs / 2);
                } else if (m.kind == ROBOT72) {
                    add(1500, m.sepMs);
                    add(1900, 1.5);
                    pixels(cr.data(), 1, m.scanMs / 2);
                    add(2300, m.sepMs);
                    add(1900, 1.5);
                    pixels(cb.data(), 1, m.scanMs / 2);
                } else {
                    pixels(cr.data(), 1, m.scanMs);
                    pixels(cb.data(), 1, m.scanMs);
                    pixels(y1.data(), 1, m.scanMs);
                }
                break;
            }
        }
        y_ += linesPerUnit(m);
        return true;
    }

    const Mode& mode_;
    LineSource source_;
    void* user_;
    double rate_;
    float amplitude_;
    std::vector<Seg> segs_;
    size_t seg_ = 0;
    double clock_ = 0, phase_ = 0;
    size_t emitted_ = 0, total_ = 0;
    int y_ = 0;
    std::vector<uint8_t> line_, line2_;
};

// ------------------------------------------------------------- receive ----

// One decoded image line, width x 3 bytes R G B.
typedef void (*LineSink)(void* user, const Mode& mode, int y, const uint8_t* rgb);

class Decoder {
public:
    explicit Decoder(int sampleRate = 8000) : rate_(sampleRate) {
        // Low-pass for the complex baseband (cut-off about 1 kHz).
        const int taps = kTaps;
        double sum = 0;
        for (int i = 0; i < taps; i++) {
            double t = i - (taps - 1) / 2.0, fc = 1000.0 / rate_;
            double h = t == 0 ? 2 * fc : std::sin(2 * M_PI * fc * t) / (M_PI * t);
            h *= 0.54 - 0.46 * std::cos(2 * M_PI * i / (taps - 1));
            lp_[i] = (float)h;
            sum += h;
        }
        for (int i = 0; i < taps; i++) lp_[i] /= (float)sum;
        ring_.assign(kRing, 0);
    }

    void onLine(LineSink sink, void* user) {
        sink_ = sink;
        user_ = user;
    }

    // Decode the next picture in this mode without waiting for a VIS header
    // (it starts at the next sync pulse).
    void force(const Mode* m) {
        forced_ = m;
        state_ = m ? FORCED : SEARCH;
    }

    void reset() {
        state_ = SEARCH;
        mode_ = nullptr;
        forced_ = nullptr;
        leader_ = leaderEnd_ = 0;
    }

    const Mode* mode() const { return mode_; }
    bool receiving() const { return state_ == IMAGE; }
    int lines() const { return lineDone_; }
    int completed() const { return completed_; }        // pictures finished so far
    float frequency() const { return smooth_; }        // for a tuning display
    double slantPpm() const { return mode_ ? (period_ / (unitMs(*mode_) * rate_ / 1000.0) - 1) * 1e6 : 0; }

    void feed(const int16_t* samples, size_t n) {
        for (size_t k = 0; k < n; k++) {
            // Mix down to baseband around 1900 Hz and low-pass.
            float s = samples[k];
            float c = (float)std::cos(nco_), si = (float)-std::sin(nco_);
            nco_ += 2 * M_PI * 1900.0 / rate_;
            if (nco_ > 2 * M_PI) nco_ -= 2 * M_PI;
            hi_[hpos_] = s * c;
            hq_[hpos_] = s * si;
            hpos_ = (hpos_ + 1) % kTaps;
            float i = 0, q = 0;
            for (int t = 0; t < kTaps; t++) {
                int idx = (hpos_ + t) % kTaps;
                i += lp_[t] * hi_[idx];
                q += lp_[t] * hq_[idx];
            }
            // Instantaneous frequency from the phase step.
            float dphi = std::atan2(q * pi_ - i * pq_, i * pi_ + q * pq_);
            pi_ = i;
            pq_ = q;
            float f = 1900.0f + dphi * (float)rate_ / (2 * (float)M_PI);
            if (f < 900) f = 900;
            if (f > 2700) f = 2700;
            smooth_ = 0.98f * smooth_ + 0.02f * f;
            // Running sum, kept per sample: windows are averaged in O(1).
            sum_ += (uint32_t)(int)f;
            ring_[(size_t)(count_ % kRing)] = sum_;
            count_++;
            step();
        }
    }

private:
    enum State { SEARCH, VIS, FORCED, IMAGE };
    static constexpr int kTaps = 21;
    static constexpr size_t kRing = 16384;      // 2 s at 8 kHz

    // Mean frequency over samples [a, b).
    double mean(double a, double b) const {
        long long ia = (long long)std::floor(a), ib = (long long)std::floor(b);
        if (ib <= ia) ib = ia + 1;
        if (ia < 1 || ib > (long long)count_ || (long long)count_ - ia >= (long long)kRing) return 1900;
        uint32_t s = ring_[(size_t)((ib - 1) % kRing)] - ring_[(size_t)((ia - 1) % kRing)];
        return (double)s / (double)(ib - ia);
    }

    double ms(double m) const { return m * rate_ / 1000.0; }

    void step() {
        long long now = (long long)count_;
        switch (state_) {
            case SEARCH: {
                // A 1900 Hz leader of at least 150 ms, then within a few ms a fall
                // towards 1200 Hz: maybe the start bit (or the 10 ms break between the leaders).
                double f = mean(now - ms(10), now);
                if (std::fabs(f - 1900) < 120) {
                    if (++leader_ > ms(150)) leaderEnd_ = now;
                } else {
                    leader_ = 0;
                }
                if (leaderEnd_ && now - leaderEnd_ < ms(20) && mean(now - ms(4), now) < 1350) {
                    edge_ = now - (long long)ms(4.4);       // where the fall began
                    leaderEnd_ = 0;
                    startChecked_ = false;
                    state_ = VIS;
                }
                break;
            }
            case VIS: {
                // The start bit lasts 30 ms; the break between the leaders only 10.
                if (now < edge_ + ms(28)) break;
                if (!startChecked_) {
                    startChecked_ = true;
                    if (std::fabs(mean(edge_ + ms(8), edge_ + ms(25)) - 1200) > 90) {
                        state_ = SEARCH;
                        leader_ = 0;
                        break;
                    }
                }
                if (now < edge_ + ms(330)) break;
                int code = 0, ones = 0;
                for (int b = 0; b < 8; b++) {
                    double f = mean(edge_ + ms(30 * (b + 1) + 6), edge_ + ms(30 * (b + 1) + 24));
                    int bit = f < 1200 ? 1 : 0;
                    ones += bit;
                    if (b < 7) code |= bit << b;
                }
                bool stopOk = std::fabs(mean(edge_ + ms(276), edge_ + ms(294)) - 1200) < 90;
                const Mode* m = byVis(code);
                if (m && stopOk && ones % 2 == 0) start(m, edge_ + ms(300) + ms(m->kind == SCOTTIE ? m->syncMs : 0), true);
                else state_ = SEARCH;
                break;
            }
            case FORCED: {
                // Wait for a sync pulse, then start there.
                const Mode* m = forced_;
                double f = mean(now - ms(m->syncMs * 0.8), now);
                if (std::fabs(f - 1200) < 60) {
                    long long syncStart = now - (long long)ms(m->syncMs * 0.8);
                    start(m, syncStart - ms(syncOffsetMs(*m)), false);
                    forced_ = nullptr;
                }
                break;
            }
            case IMAGE:
                decodeReady();
                break;
        }
    }

    void start(const Mode* m, double unitStart, bool fromVis) {
        mode_ = m;
        state_ = IMAGE;
        origin_ = unitStart;
        period_ = ms(unitMs(*m));
        unit_ = 0;
        lineDone_ = 0;
        fitN_ = 0;
        fitSk_ = fitSs_ = fitSkk_ = fitSks_ = 0;
        (void)fromVis;
        rgb_.assign((size_t)m->width * 3, 0);
        rgb2_.assign((size_t)m->width * 3, 0);
        chroma_.assign((size_t)m->width, 128);
        lumaPrev_.assign((size_t)m->width, 0);
    }

    // Expected start of unit k, from the sync pulses seen so far.
    double unitStart(int k) const { return origin_ + k * period_; }

    // Finds the sync pulse near where it should be and fits a straight line
    // through all the sync positions so far: the receiver's and the sender's
    // sample clocks never agree exactly, and the fit keeps the picture straight.
    void trackSync(int k) {
        const Mode& m = *mode_;
        double offset = ms(syncOffsetMs(m));
        double expected = unitStart(k) + offset;
        double len = ms(m.syncMs), best = 1e9, bestAt = expected;
        int range = (int)ms(4);
        for (int d = -range; d <= range; d++) {
            double f = mean(expected + d, expected + d + len);
            if (f < best) { best = f; bestAt = expected + d; }
        }
        if (best > 1350) return;                    // no clear sync: keep the timing as it is
        // Refine on the end of the pulse: it always returns to the 1500 Hz
        // porch, so the half-way point (1350 Hz) does not depend on the
        // picture, unlike the start of the pulse.
        double edge = -1;
        for (double t = bestAt + len - ms(2.5); t < bestAt + len + ms(2.5); t += 1) {
            if (mean(t - ms(0.4), t) < 1350 && mean(t, t + ms(0.4)) >= 1350) { edge = t; break; }
        }
        double s = (edge > 0 ? edge - len : bestAt) - offset;     // measured start of unit k
        fitN_++;
        fitSk_ += k;
        fitSs_ += s;
        fitSkk_ += (double)k * k;
        fitSks_ += k * s;
        double denom = fitN_ * fitSkk_ - fitSk_ * fitSk_;
        if (fitN_ >= 4 && std::fabs(denom) > 1e-9) {
            double p = (fitN_ * fitSks_ - fitSk_ * fitSs_) / denom;
            if (std::fabs(p / ms(unitMs(m)) - 1) < 0.01) {
                period_ = p;
                origin_ = (fitSs_ - p * fitSk_) / fitN_;
            }
        } else if (fitN_ < 4) {
            origin_ = s - k * period_;
        }
    }

    uint8_t level(double a, double b) const { return clamp8((mean(a, b) - 1500.0) * 255.0 / 800.0); }

    void scan(double t0, double scanMs, uint8_t* out, int stride) const {
        double each = ms(scanMs) / mode_->width;
        for (int x = 0; x < mode_->width; x++) out[x * stride] = level(t0 + x * each, t0 + (x + 1) * each);
    }

    void emit(int y, const uint8_t* rgb) {
        if (sink_ && y < mode_->height) sink_(user_, *mode_, y, rgb);
        lineDone_ = y + 1;
    }

    void decodeReady() {
        const Mode& m = *mode_;
        double end = unitStart(unit_) + period_ + ms(6);
        if ((double)count_ < end) return;
        trackSync(unit_);
        double t = unitStart(unit_);
        int w = m.width, y = unit_ * linesPerUnit(m);
        switch (m.kind) {
            case MARTIN:
                t += ms(m.syncMs + m.porchMs);
                scan(t, m.scanMs, &rgb_[1], 3);
                t += ms(m.scanMs + m.sepMs);
                scan(t, m.scanMs, &rgb_[2], 3);
                t += ms(m.scanMs + m.sepMs);
                scan(t, m.scanMs, &rgb_[0], 3);
                emit(y, rgb_.data());
                break;
            case SCOTTIE:
                t += ms(m.sepMs);
                scan(t, m.scanMs, &rgb_[1], 3);
                t += ms(m.scanMs + m.sepMs);
                scan(t, m.scanMs, &rgb_[2], 3);
                t += ms(m.scanMs + m.syncMs + m.sepMs);
                scan(t, m.scanMs, &rgb_[0], 3);
                emit(y, rgb_.data());
                break;
            case ROBOT36: {
                std::vector<uint8_t> luma(w), chroma(w);
                t += ms(m.syncMs + m.porchMs);
                scan(t, m.scanMs, luma.data(), 1);
                double sep = mean(t + ms(m.scanMs + 0.5), t + ms(m.scanMs + m.sepMs - 0.5));
                bool redLine = sep < 1900;               // 1500 Hz separator: R-Y follows
                t += ms(m.scanMs + m.sepMs + 1.5);
                scan(t, m.scanMs / 2, chroma.data(), 1);
                if (redLine) {
                    chroma_ = chroma;                    // keep it for the next (blue) line
                    lumaPrev_ = luma;
                    if (y + 1 >= m.height) {             // last line: no partner, use grey chroma
                        for (int x = 0; x < w; x++) yccToRgb(luma[x], 128, chroma[x], &rgb_[x * 3]);
                        emit(y, rgb_.data());
                    }
                } else {
                    for (int x = 0; x < w; x++) {
                        yccToRgb(lumaPrev_[x], chroma[x], chroma_[x], &rgb_[x * 3]);
                        yccToRgb(luma[x], chroma[x], chroma_[x], &rgb2_[x * 3]);
                    }
                    if (y >= 1) emit(y - 1, rgb_.data());
                    emit(y, rgb2_.data());
                }
                break;
            }
            case ROBOT72: {
                std::vector<uint8_t> luma(w), cr(w), cb(w);
                t += ms(m.syncMs + m.porchMs);
                scan(t, m.scanMs, luma.data(), 1);
                t += ms(m.scanMs + m.sepMs + 1.5);
                scan(t, m.scanMs / 2, cr.data(), 1);
                t += ms(m.scanMs / 2 + m.sepMs + 1.5);
                scan(t, m.scanMs / 2, cb.data(), 1);
                for (int x = 0; x < w; x++) yccToRgb(luma[x], cb[x], cr[x], &rgb_[x * 3]);
                emit(y, rgb_.data());
                break;
            }
            case PD: {
                std::vector<uint8_t> y0(w), cr(w), cb(w), y1(w);
                t += ms(m.syncMs + m.porchMs);
                scan(t, m.scanMs, y0.data(), 1);
                t += ms(m.scanMs);
                scan(t, m.scanMs, cr.data(), 1);
                t += ms(m.scanMs);
                scan(t, m.scanMs, cb.data(), 1);
                t += ms(m.scanMs);
                scan(t, m.scanMs, y1.data(), 1);
                for (int x = 0; x < w; x++) {
                    yccToRgb(y0[x], cb[x], cr[x], &rgb_[x * 3]);
                    yccToRgb(y1[x], cb[x], cr[x], &rgb2_[x * 3]);
                }
                emit(y, rgb_.data());
                emit(y + 1, rgb2_.data());
                break;
            }
        }
        unit_++;
        if (unit_ * linesPerUnit(m) >= m.height) {
            completed_++;
            state_ = SEARCH;
            leader_ = 0;
        }
    }

    int rate_;
    float lp_[kTaps];
    float hi_[kTaps] = {0}, hq_[kTaps] = {0};
    int hpos_ = 0;
    double nco_ = 0;
    float pi_ = 0, pq_ = 0, smooth_ = 1900;
    std::vector<uint32_t> ring_;
    uint32_t sum_ = 0;
    uint64_t count_ = 0;
    State state_ = SEARCH;
    const Mode* mode_ = nullptr;
    const Mode* forced_ = nullptr;
    long long leader_ = 0, leaderEnd_ = 0, edge_ = 0;
    bool startChecked_ = false;
    double origin_ = 0, period_ = 0;
    int unit_ = 0, lineDone_ = 0, completed_ = 0;
    int fitN_ = 0;
    double fitSk_ = 0, fitSs_ = 0, fitSkk_ = 0, fitSks_ = 0;
    std::vector<uint8_t> rgb_, rgb2_, chroma_, lumaPrev_;
    LineSink sink_ = nullptr;
    void* user_ = nullptr;
};

}  // namespace sstv
