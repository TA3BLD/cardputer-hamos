// Reads frequency and mode from a transceiver through Hamlib's rigctld
// (run "rigctld -m <model> -r <port>" on a PC or Raspberry Pi on the same
// network and put its address in Settings > Rig host). Used to fill in the
// logbook automatically.
#pragma once
#include <string>

namespace rig {
void begin();
void tick();
bool connected();
std::string freqMhz();      // "14.074", "" if unknown
std::string mode();         // logbook style: SSB, CW, FM, AM, RTTY, DATA
// Sets the radio to a frequency and (if not empty) a mode such as "SSB",
// "CW", "FT8". False when no radio is connected.
bool tune(double mhz, const std::string& mode);
}
