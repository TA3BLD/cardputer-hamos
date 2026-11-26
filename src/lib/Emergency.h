// Emergency helpers without hardware: recognising distress messages (also in
// Turkish), building the SOS text, and when to repeat it.
#pragma once
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <string>

namespace sos {

// Upper-case ASCII with the Turkish letters folded (ç->C, ğ->G, ı/i/İ->I, ö->O, ş->S, ü->U).
inline std::string fold(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            out += (char)toupper(c);
            continue;
        }
        unsigned char d = i + 1 < s.size() ? (unsigned char)s[i + 1] : 0;
        char r = 0;
        if (c == 0xC3 && (d == 0xA7 || d == 0x87)) r = 'C';
        else if (c == 0xC4 && (d == 0x9F || d == 0x9E)) r = 'G';
        else if (c == 0xC4 && (d == 0xB1 || d == 0xB0)) r = 'I';
        else if (c == 0xC3 && (d == 0xB6 || d == 0x96)) r = 'O';
        else if (c == 0xC5 && (d == 0x9F || d == 0x9E)) r = 'S';
        else if (c == 0xC3 && (d == 0xBC || d == 0x9C)) r = 'U';
        if (r) {
            out += r;
            i++;
        } else {
            out += '?';
        }
    }
    return out;
}

// True for a call for help: the words SOS, MAYDAY, EMERGENCY, ACIL (acil),
// YARDIM (help) as separate words.
inline bool isDistress(const std::string& text) {
    std::string t = fold(text);
    static const char* words[] = {"SOS", "MAYDAY", "EMERGENCY", "ACIL", "YARDIM"};
    for (const char* w : words) {
        std::string word = w;
        size_t p = 0;
        while ((p = t.find(word, p)) != std::string::npos) {
            bool before = p == 0 || !isalnum((unsigned char)t[p - 1]);
            size_t e = p + word.size();
            bool after = e >= t.size() || !isalnum((unsigned char)t[e]);
            if (before && after) return true;
            p = e;
        }
    }
    return false;
}

// "SOS TA3BLD 38.42370,27.14280 KM38nk bat 87% #2 broken leg"
inline std::string text(const std::string& call, bool havePos, double lat, double lon, const std::string& grid, int battery,
                        int number, const std::string& note) {
    std::string s = "SOS " + (call.empty() ? std::string("?") : call);
    char b[64];
    if (havePos) {
        snprintf(b, sizeof(b), " %.5f,%.5f", lat, lon);
        s += b;
    }
    if (!grid.empty()) s += " " + grid;
    if (battery >= 0) {
        snprintf(b, sizeof(b), " bat %d%%", battery);
        s += b;
    }
    s += " #" + std::to_string(number);
    if (!note.empty()) s += " " + note;
    if (s.size() > 200) s.resize(200);
    return s;
}

// Seconds after the start at which the n-th SOS (0-based) goes out: at once,
// then every minute for five minutes, then every five minutes.
inline uint32_t sendTime(int n) {
    if (n <= 0) return 0;
    if (n <= 5) return (uint32_t)n * 60;
    return 300 + (uint32_t)(n - 5) * 300;
}

}  // namespace sos
