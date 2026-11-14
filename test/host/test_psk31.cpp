// Host-side test: g++ -std=c++17 -O1 -I src test/host/test_psk31.cpp -o test/host/t.out && test/host/t.out
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include "lib/Psk31.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static std::vector<int16_t> signal(const std::string& text, double hz, double snrDb, unsigned seed, double extraHz = 0) {
    psk31::Modulator m(text, hz + extraHz, 8000, 8000);
    std::vector<int16_t> clean(8000, 0);                      // a second of silence first
    int16_t buf[512];
    for (size_t n; (n = m.read(buf, 512)) > 0;) clean.insert(clean.end(), buf, buf + n);
    clean.resize(clean.size() + 8000, 0);
    // noise for the SNR in 2500 Hz (signal power: steady carrier amplitude 8000)
    double sigma = std::sqrt(8000.0 * 8000.0 / 2 / std::pow(10, snrDb / 10) * 4000.0 / 2500.0);
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0, sigma);
    std::vector<int16_t> out;
    for (int16_t s : clean) out.push_back((int16_t)std::max(-32767.0, std::min(32767.0, s + g(rng))));
    return out;
}

static std::string decode(const std::vector<int16_t>& audio, double tuneHz, bool afc = true) {
    psk31::Demodulator d(8000);
    d.setFrequency(tuneHz);
    d.afc = afc;
    for (size_t i = 0; i < audio.size(); i += 256) d.feed(audio.data() + i, std::min<size_t>(256, audio.size() - i));
    return d.text;
}

// Characters of `want` found in order in `got` (a rough score for noisy decodes).
static double score(const std::string& want, const std::string& got) {
    size_t at = 0, hit = 0;
    for (char c : want) {
        size_t f = got.find(c, at);
        if (f != std::string::npos && f - at < 3) { hit++; at = f + 1; }
    }
    return (double)hit / want.size();
}

int main() {
    const std::string msg = "CQ CQ CQ de TA3BLD TA3BLD pse k";
    // Bits
    CHECK(psk31::encodeBits("e") == "1100" && psk31::encodeBits("a ") == "101100100");
    // Clean, exactly tuned
    std::string got = decode(signal(msg, 1000, 30, 1), 1000);
    CHECK(got.find(msg) != std::string::npos);
    if (got.find(msg) == std::string::npos) printf("  clean: \"%s\"\n", got.c_str());
    // Mixed case, punctuation, line break
    std::string text2 = "Name: Ali, QTH Izmir (KM38nk). 599?\nTNX 73!";
    got = decode(signal(text2, 1500, 30, 2), 1500);
    CHECK(got.find("Name: Ali, QTH Izmir (KM38nk). 599?\nTNX 73!") != std::string::npos);
    // Tuned off by up to 30 Hz: the AFC pulls in
    for (double off : {8.0, -15.0, 30.0}) {
        got = decode(signal(msg, 1000, 20, 3, off), 1000);
        bool ok = got.find("TA3BLD TA3BLD pse k") != std::string::npos;
        if (!ok) printf("  %+.0f Hz off: \"%s\"\n", off, got.c_str());
        CHECK(ok);
    }
    // Noise (SNR in 2500 Hz)
    for (double snr : {10.0, 0.0, -5.0, -8.0}) {
        double total = 0;
        for (unsigned seed = 10; seed < 14; seed++) total += score(msg, decode(signal(msg, 1200, snr, seed), 1200));
        printf("  SNR %+3.0f dB: %3.0f%% of the characters\n", snr, 100 * total / 4);
        if (snr >= 0) CHECK(total / 4 > 0.97);
        if (snr >= -5) CHECK(total / 4 > 0.8);
    }
    // Silence and noise alone print (almost) nothing
    std::vector<int16_t> noise = signal("", 1000, -40, 5);
    CHECK(decode(noise, 1000).size() <= 2);
    // Spectrum: the strongest bin is at the carrier
    psk31::Spectrum sp(8000);
    std::vector<int16_t> a = signal(msg, 1500, 30, 6);
    bool ready = false;
    for (size_t i = 0; i < 20000; i++) ready |= sp.feed(a[8000 + i]);
    int best = 0;
    for (int b = 1; b < psk31::Spectrum::N / 2; b++) if (sp.db[b] > sp.db[best]) best = b;
    CHECK(ready && std::fabs(best * sp.hzPerBin() - 1500) < 20);
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
