// Morse code table and timing. Pure C++, host-testable.
#pragma once
#include <cctype>
#include <cstdint>
#include <string>

namespace morse {

// Koch method teaching order.
constexpr const char* kKochOrder = "KMRSUAPTLOWI.NJEF0Y,VG5/Q9ZH38B?427C1D6X";

// Dots and dashes for a character, "" if it has no code.
inline const char* encode(char c) {
    static const char* letters[26] = {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
        "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."};
    static const char* digits[10] = {"-----", ".----", "..---", "...--", "....-",
                                     ".....", "-....", "--...", "---..", "----."};
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    switch (c) {
        case '.': return ".-.-.-";
        case ',': return "--..--";
        case '?': return "..--..";
        case '/': return "-..-.";
        case '=': return "-...-";
        case '+': return ".-.-.";
        case '-': return "-....-";
        default: return "";
    }
}

// The character for a pattern of dots and dashes, 0 if none.
inline char decode(const std::string& pattern) {
    if (pattern.empty()) return 0;
    static const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?/=+-";
    for (const char* c = kChars; *c; c++)
        if (pattern == encode(*c)) return *c;
    return 0;
}

// Length of one dot in milliseconds (PARIS standard).
inline int dotMs(int wpm) { return 1200 / (wpm < 5 ? 5 : wpm); }

// Where sending `text` is after `elapsedMs`, with the usual timing (element,
// one dot between elements, three between letters, seven between words):
// the letter (index into text), the element of that letter (-1 in the gap
// before it) and whether the key is down. False once it has all been sent.
inline bool timeline(const std::string& text, uint32_t dotMs, uint32_t elapsedMs, size_t& letter, int& element, bool& down) {
    uint32_t t = 0;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == ' ') {
            if (elapsedMs < t + 4 * dotMs) {       // 3 after the letter + 4 = 7
                letter = i;
                element = -1;
                down = false;
                return true;
            }
            t += 4 * dotMs;
            continue;
        }
        const char* p = encode(text[i]);
        for (int e = 0; p[e]; e++) {
            uint32_t on = (p[e] == '-' ? 3 : 1) * dotMs, off = p[e + 1] ? dotMs : 3 * dotMs;
            if (elapsedMs < t + on + off) {
                letter = i;
                element = e;
                down = elapsedMs < t + on;
                return true;
            }
            t += on + off;
        }
    }
    return false;
}

}  // namespace morse
