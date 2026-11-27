// Paddle keyer and decoding what is keyed. Pure logic: feed it the time and
// the contacts often (every few milliseconds); it says whether the key is down
// and collects the letters sent.
//   Iambic (mode B): a held paddle repeats, squeezing alternates dit and dah.
//   Simple (setSimple): each press gives one dit or one dah, no repeat.
//   StraightKey: one contact, the operator times the elements.
#pragma once
#include <cstdint>
#include <string>
#include "Morse.h"

namespace morse {

class Iambic {
public:
    void setWpm(int wpm) { dot_ = (uint32_t)dotMs(wpm); }
    void setSimple(bool on) { simple_ = on; }

    // Returns true while the key (and sidetone) should be down.
    bool update(uint32_t now, bool dit, bool dah) {
        if (simple_) {              // only a new press starts an element
            bool newDit = dit && !prevDit_, newDah = dah && !prevDah_;
            prevDit_ = dit;
            prevDah_ = dah;
            if (state_ != IDLE) {   // a press during an element waits for the next idle
                pendDit_ = pendDit_ || newDit;
                pendDah_ = pendDah_ || newDah;
            } else {
                newDit = newDit || pendDit_;
                newDah = newDah || pendDah_;
                pendDit_ = pendDah_ = false;
            }
            dit = newDit;
            dah = newDah && !newDit;
        } else if (state_ == ELEMENT || state_ == GAP) {       // remember the other paddle (mode B)
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
            if (!simple_ && (ditMem_ || dahMem_)) {
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
                if (!spaced_ && pattern_.empty() && now - idleSince_ >= 6 * dot_) {     // after a letter, even if taken
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

    // The letter being keyed: its dots and dashes so far, and whether one sounds now.
    const std::string& pattern() const { return pattern_; }
    bool sounding() const { return state_ == ELEMENT; }

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
    bool simple_ = false, prevDit_ = false, prevDah_ = false, pendDit_ = false, pendDah_ = false;
    std::string pattern_, text_;
};

// Straight key: the key is down exactly while the contact is closed. A press
// shorter than two dots is a dit, longer a dah; the dot length follows the
// operator's own speed, starting from setWpm().
class StraightKey {
public:
    void setWpm(int wpm) { dot_ = (uint32_t)morse::dotMs(wpm); }
    uint32_t dotMs() const { return dot_; }     // the operator's dot length now

    bool update(uint32_t now, bool down) {
        if (down && !down_) {
            down_ = true;
            since_ = now;
        } else if (!down && down_) {
            down_ = false;
            uint32_t len = now - since_;
            since_ = now;
            if (len < 10) return false;             // contact bounce
            bool dah = len >= 2 * dot_;
            pattern_ += dah ? '-' : '.';
            uint32_t est = dah ? len / 3 : len;     // follow the operator's speed slowly
            dot_ = (3 * dot_ + est) / 4;
            if (dot_ < 20) dot_ = 20;
            if (dot_ > 400) dot_ = 400;
        } else if (!down_) {
            uint32_t idle = now - since_;
            if (!pattern_.empty() && idle >= 5 * dot_ / 2) {      // letter gap (3 dots, a little early)
                char c = decode(pattern_);
                text_ += c ? c : '*';
                pattern_.clear();
                spaced_ = false;
            }
            if (!spaced_ && pattern_.empty() && idle >= 6 * dot_) {
                text_ += ' ';
                spaced_ = true;
            }
        }
        return down_;
    }

    // The letter being keyed, with the element held right now (a dot that
    // turns into a dash once held for two dots).
    std::string live(uint32_t now) const {
        if (!down_) return pattern_;
        return pattern_ + (now - since_ >= 2 * dot_ ? '-' : '.');
    }
    bool sounding() const { return down_; }

    const std::string& text() const { return text_; }
    std::string take() {
        std::string t;
        t.swap(text_);
        return t;
    }

private:
    uint32_t dot_ = 60, since_ = 0;
    bool down_ = false, spaced_ = true;
    std::string pattern_, text_;
};

}  // namespace morse
