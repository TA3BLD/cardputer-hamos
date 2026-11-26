// Small command line helper used by tools/ft8/crosscheck.py:
//   ft8_tool payload "CQ TA3BLD KM38"     -> 77 bits as 0/1, then the 79 tones
//   ft8_tool unpack <77 bits as 0/1>       -> text
//   ft8_tool encode out.raw rate f0 "MSG" [f0 "MSG" ...]   -> int16 mono audio, signals start at 0.5 s
//   ft8_tool decode in.raw rate osr        -> decoded messages, one per line: snr dt freq text
// Build: g++ -std=c++17 -O2 -I src test/host/ft8_tool.cpp -o ft8_tool
#include <cstdio>
#include <cstdlib>
#include "lib/Ft8.h"
#include "lib/Ft8Decoder.h"

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "payload" && argc == 3) {
        uint8_t p[10], t[ft8::kSymbols];
        if (!ft8::pack(argv[2], p)) { printf("ERROR\n"); return 0; }
        for (int i = 0; i < 77; i++) putchar('0' + (int)ft8::getBits(p, i, 1));
        putchar('\n');
        ft8::tones(p, t);
        for (int i = 0; i < ft8::kSymbols; i++) putchar('0' + t[i]);
        printf("\n%s\n", ft8::unpack(p).c_str());
        return 0;
    }
    if (mode == "unpack" && argc == 3) {
        uint8_t p[10] = {0};
        for (int i = 0; i < 77 && argv[2][i]; i++) ft8::putBits(p, i, 1, argv[2][i] == '1');
        printf("%s\n", ft8::unpack(p).c_str());
        return 0;
    }
    if (mode == "encode" && argc >= 6) {
        int rate = atoi(argv[3]);
        std::vector<float> mix((size_t)(15 * rate), 0.0f);
        for (int a = 4; a + 1 < argc; a += 2) {
            uint8_t p[10], t[ft8::kSymbols];
            if (!ft8::pack(argv[a + 1], p)) { fprintf(stderr, "cannot pack %s\n", argv[a + 1]); return 1; }
            ft8::tones(p, t);
            ft8::Synth synth(t, (float)atof(argv[a]), rate, 4000, 0.5f);
            int16_t buf[512];
            size_t pos = 0;
            for (size_t n; (n = synth.read(buf, 512)) > 0;)
                for (size_t i = 0; i < n && pos < mix.size(); i++) mix[pos++] += buf[i];
        }
        FILE* f = fopen(argv[2], "wb");
        for (float v : mix) { int16_t s = (int16_t)v; fwrite(&s, 2, 1, f); }
        fclose(f);
        return 0;
    }
    if (mode == "decode" && argc == 5) {
        int rate = atoi(argv[3]);
        FILE* f = fopen(argv[2], "rb");
        if (!f) return 1;
        std::vector<int16_t> audio;
        int16_t s;
        while (fread(&s, 2, 1, f) == 1) audio.push_back(s);
        fclose(f);
        ft8::Decoder decoder;
        if (!decoder.begin(atoi(argv[4]))) return 1;
        ft8::Resampler resampler;
        for (size_t i = 0; i < audio.size() && !decoder.full(); i += 256) {
            size_t n = std::min<size_t>(256, audio.size() - i);
            if (rate == 8000) {
                int16_t out[256];
                size_t m = resampler.process(audio.data() + i, n, out);
                decoder.feed(out, m);
            } else {
                decoder.feed(audio.data() + i, n);        // already 6400 Hz
            }
        }
        std::vector<ft8::Decoded> found;
        decoder.startDecode();
        while (decoder.step(found)) {}
        for (const ft8::Decoded& d : found) printf("%+d %.2f %.1f %s\n", d.snr, d.dt, d.freqHz, d.text.c_str());
        return 0;
    }
    fprintf(stderr, "usage: see the top of ft8_tool.cpp\n");
    return 2;
}
