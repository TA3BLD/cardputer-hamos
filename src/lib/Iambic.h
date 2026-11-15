// Iambic paddle keyer (mode B) and decoding what is keyed. Pure logic: feed it
// the time and both paddle contacts often (every few milliseconds); it says
// whether the key is down and collects the letters sent.
#pragma once
#include <cstdint>
#include <string>
#include "Morse.h"

namespace morse {

class Iambic {
public:
    void setWpm(int wpm) { dot_ = (uint32_t)dotMs(wpm); }

    // Returns true while the key (and sidetone) should be down.
    bool update(uint32_t now, bool dit, bool dah) {
        if (state_ == ELEMENT || state_ == GAP) {       // remember the other paddle (mode B)
            if (dit && lastDah_) ditMem_ = true;
            if (dah && !lastDah_) dahMem_ = true;
        }
        if (state_ == ELEMENT && (int32_t)(now - until_) >= 0) {
            state_ = GAP;
            until_ = now + dot_;
        }
        if (state_ == GAP && (int32_t)(now - until_) >= 0) {
            state_ = IDLE;
            idleSince_ = now;
        }
        if (state_ == IDLE) {
            bool sendDah;
            if (ditMem_ || dahMem_) {
                sendDah = dahMem_ && (!ditMem_ || !lastDah_);
            } else if (dit && dah) {
                sendDah = !lastDah_;                    // squeeze: alternate
            } else if (dit || dah) {
                sendDah = dah;
            } else {
                // Gaps: letter after 2 more dots of silence (3 in all), word after 6.
                if (!pattern_.empty() && now - idleSince_ >= 2 * dot_) {
                    char c = decode(pattern_);
                    text_ += c ? c : '*';
                    pattern_.clear();
                    spaced_ = false;
                }
                if (!spaced_ && pattern_.empty() && !text_.empty() && text_.back() != ' ' && now - idleSince_ >= 6 * dot_) {
                    text_ += ' ';
                    spaced_ = true;
                }
                return false;
            }
            ditMem_ = dahMem_ = false;
            lastDah_ = sendDah;
            pattern_ += sendDah ? '-' : '.';
            state_ = ELEMENT;
            until_ = now + (sendDah ? 3 * dot_ : dot_);
        }
        return state_ == ELEMENT;
    }

    // Letters keyed so far; take() empties it.
    const std::string& text() const { return text_; }
    std::string take() {
        std::string t;
        t.swap(text_);
        return t;
    }

private:
    enum { IDLE, ELEMENT, GAP } state_ = IDLE;
    uint32_t dot_ = 60, until_ = 0, idleSince_ = 0;
    bool lastDah_ = true, ditMem_ = false, dahMem_ = false, spaced_ = true;
    std::string pattern_, text_;
};

}  // namespace morse
