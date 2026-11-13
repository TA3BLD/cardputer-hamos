// Morse code table and timing. Pure C++, host-testable.
#pragma once
#include <cctype>
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

// Length of one dot in milliseconds (PARIS standard).
inline int dotMs(int wpm) { return 1200 / (wpm < 5 ? 5 : wpm); }

}  // namespace morse
