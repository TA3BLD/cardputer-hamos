#include "Lang.h"
#include <cstring>
#include <unordered_map>
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

struct Hash {
    size_t operator()(const char* s) const {
        size_t h = 2166136261u;
        for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
        return h;
    }
};
struct Eq {
    bool operator()(const char* a, const char* b) const { return strcmp(a, b) == 0; }
};

int state = -1;     // -1 not read yet, 0 English, 1 Turkish

const std::unordered_map<const char*, const char*, Hash, Eq>& table() {
    static std::unordered_map<const char*, const char*, Hash, Eq> t;
    if (t.empty())
        for (const Pair& p : kPairs) t[p.en] = p.tr;
    return t;
}

}  // namespace

bool turkish() {
    if (state < 0) state = config::get("lang", "tr") == "tr" ? 1 : 0;
    return state == 1;
}

void reload() { state = -1; }

const char* tr(const char* english) {
    if (!turkish()) return english;
    auto it = table().find(english);
    return it == table().end() ? english : it->second;
}

}  // namespace lang
