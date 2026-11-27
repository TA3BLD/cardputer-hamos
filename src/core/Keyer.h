// CW keyer: sends text (memories, typed) and reads an iambic paddle.
//
// Where the CW goes, in this order:
//   - Grove port set to "key": G1 keys the radio (through a transistor or
//     optocoupler to the KEY jack), with sidetone on the speaker
//   - a radio with CAT CW (CI-V, Kenwood, rigctld): the radio's own keyer
//   - otherwise sidetone only, for practice
// A paddle or straight key on chosen pins (Settings "Paddle ...": Grove G1/G2,
// or without the cap the header pins G3 G4 G5 G6 G13 G15; contacts to GND or
// to 3V3; iambic, simple or straight key; dit/dah swapped) gives sidetone and
// the letters on screen. In the CW keyer it keys the radio (Grove key line,
// or with CAT CW word by word), on other pages too unless "Paddle TX on all
// pages" is 0; in Morse practice it never transmits.
#pragma once
#include <cstdint>
#include <string>

namespace keyer {

const char* outputName();
void send(const std::string& text, bool sidetoneOnly = false);     // sidetoneOnly: practice, never keys the radio
void stop();
bool busy();
void tick();                    // from the main loop, every few milliseconds
bool paddle();                  // a paddle / key is set up and working
std::string paddleText();       // letters keyed with the paddle (last 60)
void clearPaddleText();
std::string takePaddleLetters();    // letters keyed since the last call
std::string paddleInfo();       // "G3/G4 GND iambic", or why the paddle is off ("" when not wanted)
bool paddleError();             // the paddle is wanted but cannot work (paddleInfo says why)
void setupPaddle();             // (re)reads the paddle settings and sets the pins up
bool paddleUsesCapPins();       // the paddle holds pins that the LoRa / GNSS cap needs
// Morse practice: the paddle at its own speed, sidetone only.
void setPractice(bool on, int wpm = 0);
// The CW keyer page is open: there the paddle always keys the radio; on other
// pages only with "Paddle TX on all pages" (paddle_tx_all, default on).
void setKeyerPage(bool open);
bool onAir();                   // the keyer keys the radio now (or did a moment ago): TX in the status bar
// What the paddle is keying right now: the dots and dashes of the letter so
// far (the last one sounding while `down`). Empty between letters.
std::string paddlePattern(bool* down = nullptr);

// Progress of send(), for a TX display: the text (upper case), the letter and
// its element being keyed (-1 in a gap), key down. With CAT CW the radio does
// the timing, so the position is estimated from the speed.
struct TxView {
    std::string text;
    size_t letter = 0;
    int element = -1;
    bool down = false, active = false, estimated = false;
    uint32_t doneMsAgo = 0xFFFFFFFF;    // since it finished (not after stop())
};
TxView txView();
int wpm();
void setWpm(int wpm);

// Memories with placeholders: {MYCALL} {CALL} {RST} {NR} {PARK} {SUMMIT} {NAME}
std::string expand(const std::string& text, const std::string& call, const std::string& nr);
std::string memory(int n);      // 1..6, from Settings (cw_m1..cw_m6) or a default

}  // namespace keyer
