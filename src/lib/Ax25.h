// AX.25 UI frames and the Bell 202 AFSK modem used by APRS on VHF
// (1200 baud, 1200/2200 Hz, NRZI, HDLC bit stuffing). Pure C++, host-testable.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace ax25 {

constexpr int SAMPLE_RATE = 9600, BAUD = 1200, SAMPLES_PER_BIT = SAMPLE_RATE / BAUD;
constexpr double MARK_HZ = 1200.0, SPACE_HZ = 2200.0, PI2 = 6.283185307179586;

inline uint16_t crc(const uint8_t* data, size_t len) {      // CRC-16/X.25
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int b = 0; b < 8; b++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0x8408) : (uint16_t)(c >> 1);
    }
    return (uint16_t)~c;
}

inline void putAddress(std::string& out, const std::string& call, bool last, bool repeated = false) {
    size_t dash = call.find('-');
    std::string base = call.substr(0, dash);
    int ssid = dash == std::string::npos ? 0 : atoi(call.c_str() + dash + 1);
    base.resize(6, ' ');
    for (char c : base) out += (char)(toupper((unsigned char)c) << 1);
    out += (char)(0x60 | ((ssid & 15) << 1) | (last ? 1 : 0) | (repeated ? 0x80 : 0));
}

// "SRC>DST,PATH1,PATH2:info" -> frame bytes including the checksum.
inline bool encodeFrame(const std::string& tnc2, std::string& frame) {
    size_t gt = tnc2.find('>'), colon = tnc2.find(':');
    if (gt == std::string::npos || colon == std::string::npos || colon < gt) return false;
    std::vector<std::string> route;
    std::string rest = tnc2.substr(gt + 1, colon - gt - 1);
    for (size_t pos = 0; pos <= rest.size();) {
        size_t comma = rest.find(',', pos);
        if (comma == std::string::npos) comma = rest.size();
        route.push_back(rest.substr(pos, comma - pos));
        pos = comma + 1;
    }
    if (route.empty() || route[0].empty() || route.size() > 9) return false;
    frame.clear();
    putAddress(frame, route[0], false);
    putAddress(frame, tnc2.substr(0, gt), route.size() == 1);
    for (size_t i = 1; i < route.size(); i++) {
        std::string hop = route[i];
        bool repeated = !hop.empty() && hop.back() == '*';
        if (repeated) hop.pop_back();
        putAddress(frame, hop, i + 1 == route.size(), repeated);
    }
    frame += (char)0x03;    // UI frame
    frame += (char)0xF0;    // no layer 3
    frame += tnc2.substr(colon + 1);
    uint16_t fcs = crc((const uint8_t*)frame.data(), frame.size());
    frame += (char)(fcs & 0xFF);
    frame += (char)(fcs >> 8);
    return true;
}

inline std::string getAddress(const uint8_t* p, bool& last, bool& repeated) {
    std::string call;
    for (int i = 0; i < 6; i++) {
        char c = (char)(p[i] >> 1);
        if (c != ' ') call += c;
    }
    int ssid = (p[6] >> 1) & 15;
    if (ssid) call += "-" + std::to_string(ssid);
    last = p[6] & 1;
    repeated = p[6] & 0x80;
    return call;
}

// Frame bytes (checksum included and verified) -> "SRC>DST,PATH:info".
inline bool decodeFrame(const uint8_t* data, size_t len, std::string& tnc2) {
    if (len < 18 || crc(data, len - 2) != (uint16_t)(data[len - 2] | (data[len - 1] << 8))) return false;
    bool last = false, repeated = false;
    std::string dst = getAddress(data, last, repeated);
    if (last) return false;
    std::string src = getAddress(data + 7, last, repeated);
    size_t pos = 14;
    std::string path;
    while (!last) {
        if (pos + 7 > len - 2) return false;
        path += "," + getAddress(data + pos, last, repeated);
        if (repeated) path += "*";
        pos += 7;
    }
    if (pos + 2 > len - 2 || data[pos] != 0x03 || data[pos + 1] != 0xF0) return false;
    tnc2 = src + ">" + dst + path + ":" + std::string((const char*)data + pos + 2, len - 2 - pos - 2);
    return true;
}

// Audio for one frame: flags, bit-stuffed NRZI data, flags.
inline std::vector<int16_t> modulate(const std::string& frame, int leadFlags = 45, int tailFlags = 5, int amplitude = 12000) {
    std::vector<uint8_t> bits;
    auto flag = [&]() { for (int i = 0; i < 8; i++) bits.push_back((0x7E >> i) & 1); };
    for (int i = 0; i < leadFlags; i++) flag();
    int ones = 0;
    for (unsigned char byte : frame)
        for (int i = 0; i < 8; i++) {
            uint8_t bit = (byte >> i) & 1;
            bits.push_back(bit);
            ones = bit ? ones + 1 : 0;
            if (ones == 5) { bits.push_back(0); ones = 0; }     // bit stuffing
        }
    for (int i = 0; i < tailFlags; i++) flag();

    std::vector<int16_t> out;
    out.reserve(bits.size() * SAMPLES_PER_BIT);
    double phase = 0;
    bool mark = true;
    for (uint8_t bit : bits) {
        if (!bit) mark = !mark;                                 // NRZI: a zero is a tone change
        double step = PI2 * (mark ? MARK_HZ : SPACE_HZ) / SAMPLE_RATE;
        for (int i = 0; i < SAMPLES_PER_BIT; i++) {
            out.push_back((int16_t)(amplitude * std::sin(phase)));
            phase += step;
        }
        if (phase > PI2 * 1000) phase = std::fmod(phase, PI2);
    }
    return out;
}

// Streaming demodulator: feed samples, collect decoded frames as TNC2 text.
class Demodulator {
public:
    Demodulator() {
        for (int i = 0; i < SAMPLES_PER_BIT; i++) {
            markI_[i] = (float)std::cos(PI2 * MARK_HZ * i / SAMPLE_RATE);
            markQ_[i] = (float)std::sin(PI2 * MARK_HZ * i / SAMPLE_RATE);
            spaceI_[i] = (float)std::cos(PI2 * SPACE_HZ * i / SAMPLE_RATE);
            spaceQ_[i] = (float)std::sin(PI2 * SPACE_HZ * i / SAMPLE_RATE);
        }
    }

    std::vector<std::string> packets;   // decoded since the caller last cleared it
    uint32_t frames = 0, crcErrors = 0;

    void feed(const int16_t* samples, size_t n) {
        for (size_t k = 0; k < n; k++) {
            window_[head_] = samples[k];
            head_ = (head_ + 1) % SAMPLES_PER_BIT;
            // Correlate the last bit period against both tones.
            float mi = 0, mq = 0, si = 0, sq = 0;
            for (int i = 0; i < SAMPLES_PER_BIT; i++) {
                float s = window_[(head_ + i) % SAMPLES_PER_BIT];
                mi += s * markI_[i]; mq += s * markQ_[i];
                si += s * spaceI_[i]; sq += s * spaceQ_[i];
            }
            bool mark = (mi * mi + mq * mq) > (si * si + sq * sq);

            // Bit clock: sample in the middle of a bit, re-centre on every tone change.
            if (mark != lastTone_) {
                lastTone_ = mark;
                clock_ = SAMPLES_PER_BIT / 2;
            }
            if (++clock_ >= SAMPLES_PER_BIT) {
                clock_ = 0;
                pushBit(mark == sampledTone_);      // NRZI: no change = 1
                sampledTone_ = mark;
            }
        }
    }

private:
    void pushBit(bool one) {       // (not "bit": Arduino defines a macro with that name)
        shift_ = (uint8_t)((shift_ >> 1) | (one ? 0x80 : 0));
        if (shift_ == 0x7E) {                   // flag: end of one frame, start of the next
            if (inFrame_ && bitCount_ == 7 && frame_.size() >= 18) {
                std::string text;
                if (decodeFrame((const uint8_t*)frame_.data(), frame_.size(), text)) {
                    packets.push_back(text);
                    frames++;
                } else {
                    crcErrors++;
                }
            }
            inFrame_ = true;
            frame_.clear();
            bitCount_ = 0;
            ones_ = 0;
            current_ = 0;
            return;
        }
        if (!inFrame_) return;
        if (one) {
            if (++ones_ > 6) { inFrame_ = false; return; }      // abort / noise
        } else {
            bool stuffed = ones_ == 5;
            ones_ = 0;
            if (stuffed) return;                                // drop the stuffed zero
        }
        current_ = (uint8_t)((current_ >> 1) | (one ? 0x80 : 0));
        if (++bitCount_ == 8) {
            frame_ += (char)current_;
            bitCount_ = 0;
            if (frame_.size() > 330) inFrame_ = false;
        }
    }

    float markI_[SAMPLES_PER_BIT], markQ_[SAMPLES_PER_BIT], spaceI_[SAMPLES_PER_BIT], spaceQ_[SAMPLES_PER_BIT];
    float window_[SAMPLES_PER_BIT] = {0};
    int head_ = 0, clock_ = 0;
    bool lastTone_ = false, sampledTone_ = false, inFrame_ = false;
    uint8_t shift_ = 0, current_ = 0;
    int bitCount_ = 0, ones_ = 0;
    std::string frame_;
};

}  // namespace ax25
