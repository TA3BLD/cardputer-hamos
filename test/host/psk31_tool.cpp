// psk31_tool decode in.raw RATE TUNE_HZ   -> prints the decoded text (int16 mono input)
// Build: g++ -std=c++17 -O2 -I src test/host/psk31_tool.cpp -o psk31_tool
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "lib/Psk31.h"

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    FILE* f = fopen(argv[2], "rb");
    if (!f) return 1;
    std::vector<int16_t> a;
    int16_t s;
    while (fread(&s, 2, 1, f) == 1) a.push_back(s);
    fclose(f);
    psk31::Demodulator d(atoi(argv[3]));
    d.setFrequency(atof(argv[4]));
    for (size_t i = 0; i < a.size(); i += 256) d.feed(a.data() + i, std::min<size_t>(256, a.size() - i));
    printf("%s\n", d.text.c_str());
    return 0;
}
