// FT4 command line tool for tools/ft8/crosscheck_ft4.py.
//   ft4_tool tones "CQ TA3BLD KM38"          -> 103 tones
//   ft4_tool synth "CQ TA3BLD KM38" 1000 out.raw   -> 7.5 s at 8000 Hz, signal from 0.5 s
//   ft4_tool decode in.raw                    -> one line per decode: snr dt freq text
#include <cstdio>
#include <cstring>
#include <vector>
#include "lib/Ft4.h"

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "tones")) {
        uint8_t payload[10], t[ft4::kSymbols];
        if (!ft8::pack(argv[2], payload)) return 1;
        ft4::tones(payload, t);
        for (int i = 0; i < ft4::kSymbols; i++) printf("%d", t[i]);
        printf("\n");
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "synth")) {
        uint8_t payload[10], t[ft4::kSymbols];
        if (!ft8::pack(argv[2], payload)) return 1;
        ft4::tones(payload, t);
        ft4::Synth s(t, (float)atof(argv[3]), 8000, 12000, ft4::kNominalStartSeconds);
        std::vector<int16_t> out(60000, 0);
        s.read(out.data(), out.size());
        FILE* f = fopen(argv[4], "wb");
        fwrite(out.data(), 2, out.size(), f);
        fclose(f);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "decode")) {
        FILE* f = fopen(argv[2], "rb");
        if (!f) return 1;
        std::vector<int16_t> in(60000, 0);
        size_t n = fread(in.data(), 2, in.size(), f);
        fclose(f);
        ft4::Resampler r;
        ft4::Decoder d;
        if (!d.begin(2)) return 2;
        std::vector<int16_t> up(n * 4 / 3 + 8);
        size_t m = r.process(in.data(), n, up.data());
        d.feed(up.data(), m);
        d.startDecode();
        std::vector<ft8::Decoded> out;
        while (d.step(out, 4)) {}
        for (const auto& x : out) printf("%d %.2f %.1f %s\n", x.snr, x.dt, x.freqHz, x.text.c_str());
        return 0;
    }
    fprintf(stderr, "usage: see the top of the file\n");
    return 1;
}
