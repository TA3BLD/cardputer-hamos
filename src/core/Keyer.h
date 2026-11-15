// CW keyer: sends text (memories, typed) and reads an iambic paddle.
//
// Where the CW goes, in this order:
//   - Grove port set to "key": G1 keys the radio (through a transistor or
//     optocoupler to the KEY jack), with sidetone on the speaker
//   - a radio with CAT CW (CI-V, Kenwood, rigctld): the radio's own keyer
//   - otherwise sidetone only, for practice
// A paddle on the Grove port ("paddle": G1 dit, G2 dah, to GND) gives
// sidetone and the letters on screen; with CAT CW they are sent word by word.
#pragma once
#include <string>

namespace keyer {

const char* outputName();
void send(const std::string& text);
void stop();
bool busy();
void tick();                    // from the main loop, every few milliseconds
bool paddle();                  // a paddle is connected (Grove set to "paddle")
std::string paddleText();       // letters keyed with the paddle (last 60)
void clearPaddleText();
int wpm();
void setWpm(int wpm);

// Memories with placeholders: {MYCALL} {CALL} {RST} {NR} {PARK} {SUMMIT} {NAME}
std::string expand(const std::string& text, const std::string& call, const std::string& nr);
std::string memory(int n);      // 1..6, from Settings (cw_m1..cw_m6) or a default

}  // namespace keyer
