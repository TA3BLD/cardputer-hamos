#pragma once
#include <string>
#include "core/Keyer.h"
#include "core/Lang.h"
#include "core/Ui.h"
#include "lib/Morse.h"

// Drawing Morse code as dots and dashes (small bars), with the element being
// keyed lit: red while the key is down. Used for the TX display of the CW
// keyer and for what is keyed or played in Morse practice.
namespace cwview {

constexpr uint16_t C_ON = ui::C_ERR;        // key down now
constexpr uint16_t C_NEXT = 0xBDF7;         // still to go: light grey (sent ones are dim)

// One letter's pattern at (x, y). `current`: index of the element keyed now
// (those before it are dim), -1 for none; `all` colours every element.
// scale 1: a dot is 5x5, a dash 15x5. Returns the width drawn.
inline int pattern(int x, int y, const std::string& p, int current, bool down, int scale, uint16_t all) {
    int u = 5 * scale, gap = 4 * scale, x0 = x;
    for (int i = 0; i < (int)p.size(); i++) {
        int w = p[i] == '-' ? 3 * u : u;
        uint16_t c = all;
        if (current >= 0) c = i < current ? ui::C_DIM : i == current ? (down ? C_ON : ui::C_WARN) : C_NEXT;
        ui::fill(x, y, w, u, c);
        x += w + gap;
    }
    return x > x0 ? x - x0 - gap : 0;
}

// What is being keyed right now: the elements so far, the last one red
// while the key is down.
inline int live(int x, int y, const std::string& p, bool down, int scale) {
    int u = 5 * scale, gap = 4 * scale, x0 = x;
    for (int i = 0; i < (int)p.size(); i++) {
        int w = p[i] == '-' ? 3 * u : u;
        ui::fill(x, y, w, u, down && i + 1 == (int)p.size() ? C_ON : ui::C_TEXT);
        x += w + gap;
    }
    return x > x0 ? x - x0 - gap : 0;
}

// Width of a pattern drawn with pattern().
inline int width(const std::string& p, int scale) {
    int w = 0;
    for (char e : p) w += (e == '-' ? 15 : 5) * scale + 4 * scale;
    return w ? w - 4 * scale : 0;
}

// A pattern centred on the screen.
inline void centred(int y, const std::string& p, int current, bool down, int scale, uint16_t all) {
    pattern((ui::W - width(p, scale)) / 2, y, p, current, down, scale, all);
}

// The TX display, two lines from y: the text with the letter being sent
// highlighted, and a tape of dots and dashes from that letter on. "Sent OK"
// for a few seconds afterwards. False when there is nothing to show.
inline bool tx(int y, const keyer::TxView& v) {
    bool recent = !v.active && v.doneMsAgo < 3000;
    if (!v.active && !recent) return false;
    if (recent) {
        ui::text(2, y, T("Sent OK"), ui::C_OK);
        std::string t = v.text.size() > 30 ? v.text.substr(v.text.size() - 30) : v.text;
        ui::text(50, y, t, ui::C_DIM);
        return true;
    }
    ui::text(2, y, "TX", v.down ? C_ON : ui::C_WARN);
    if (v.estimated) ui::text(14, y, "~", ui::C_DIM);      // the radio times it: our estimate
    size_t first = v.letter > 12 ? v.letter - 12 : 0;
    int x = 26;
    for (size_t i = first; i < v.text.size() && x < ui::W - 6; i++, x += 6) {
        uint16_t c = i < v.letter ? ui::C_DIM : i == v.letter ? ui::C_WARN : ui::C_TEXT;
        ui::text(x, y, std::string(1, v.text[i]), c);
    }
    // The tape: this letter (lit element) and the ones after it.
    x = 26;
    int ty = y + 11;
    for (size_t i = v.letter; i < v.text.size() && x < ui::W - 20; i++) {
        if (v.text[i] == ' ') {
            x += 12;
            continue;
        }
        const char* p = morse::encode(v.text[i]);
        if (!*p) continue;
        bool here = i == v.letter;
        x += pattern(x, ty, p, here ? (v.element < 0 ? 0 : v.element) : -1, here && v.down, 1, C_NEXT) + 10;
    }
    return true;
}

}  // namespace cwview
