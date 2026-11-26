// Voice keyer: records short messages ("CQ CQ ...") from the microphone to the
// SD card and plays them while transmitting (PTT through CAT or the Grove
// port). The speaker plays into the radio's microphone; hold the radio's
// microphone close to the speaker, or use VOX.
#pragma once
#include <string>

namespace voice {

constexpr int kMemories = 4;
constexpr int kRate = 8000;
constexpr int kMaxSeconds = 30;

bool startRecording(int n);         // 1..4
void stopRecording();
bool recording();
int level();                        // 0..100, while recording
float seconds();                    // recorded so far
bool play(int n, bool transmit);    // transmit: PTT on while playing
void stop();
bool playing();
bool exists(int n);
bool erase(int n);                  // deletes recording n; false if there was none
float length(int n);                // seconds, 0 if none
void tick();

}  // namespace voice
