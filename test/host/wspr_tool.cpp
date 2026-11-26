// wspr_tool CALL GRID DBM [out.raw] -> 162 tones; with a file also the audio
// (2 minutes at 8000 Hz, 1500 Hz, from 1 s). For tools/ft8/crosscheck_wspr.py.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "lib/Wspr.h"
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    uint8_t t[wspr::kSymbols];
    if (!wspr::tones(argv[1], argv[2], atoi(argv[3]), t)) { printf("invalid\n"); return 2; }
    for (int i = 0; i < wspr::kSymbols; i++) printf("%d", t[i]);
    printf("\n");
    if (argc > 4) {
        wspr::Synth synth(t, 1500.0f, 8000, 8000, 1.0f);
        std::vector<int16_t> out(120 * 8000, 0);
        synth.read(out.data(), out.size());
        FILE* f = fopen(argv[4], "wb");
        fwrite(out.data(), 2, out.size(), f);
        fclose(f);
    }
    return 0;
}
