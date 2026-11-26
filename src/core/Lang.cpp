#include "Lang.h"
#include <cstring>
#include <algorithm>
#include <cstdint>
#include "Config.h"

namespace lang {

namespace {

struct Pair {
    const char* en;
    const char* tr;
};

// English -> Turkish. Keep the Turkish about as long as the English: the
// screen is 40 characters wide at the normal size.
const Pair kPairs[] = {
#include "LangTr.inc"
};

int state = -1;     // -1 not read yet, 0 English, 1 Turkish

// The pairs sorted by their English text, as indexes (2 bytes each): a hash
// map of the ~800 pairs would take about 30 KB of RAM.
constexpr size_t kCount = sizeof(kPairs) / sizeof(kPairs[0]);
const uint16_t* order() {
    static uint16_t idx[kCount];
    static bool built = false;
    if (!built) {
        for (size_t i = 0; i < kCount; i++) idx[i] = (uint16_t)i;
        std::sort(idx, idx + kCount, [](uint16_t x, uint16_t y) { return strcmp(kPairs[x].en, kPairs[y].en) < 0; });
        built = true;
    }
    return idx;
}

}  // namespace

bool turkish() {
    if (state < 0) state = config::get("lang", "tr") == "tr" ? 1 : 0;
    return state == 1;
}

void reload() { state = -1; }

const char* tr(const char* english) {
    if (!turkish()) return english;
    const uint16_t* idx = order();
    size_t lo = 0, hi = kCount;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(kPairs[idx[mid]].en, english);
        if (c == 0) return kPairs[idx[mid]].tr;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return english;
}

}  // namespace lang
