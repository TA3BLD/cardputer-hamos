// Control of the transceiver ("CAT"): frequency and mode for the logbook and
// spots, transmit (PTT) for the voice keyer, CW text for the CW keyer.
//
// "Rig type" in Settings chooses how:
//   rigctld  Hamlib's rigctld on a PC / Raspberry Pi in the network (Rig host)
//   civ      Icom CI-V cable on the Grove port (CI-V address, e.g. 94 = IC-7300)
//   ft817    Yaesu FT-817 / 857 / 897 cable on the Grove port
//   kenwood  Kenwood / Elecraft cable on the Grove port
//   yaesu    newer Yaesu (FT-991, FT-891, FT-710, FTDX10) cable on the Grove port
//   ic705    Icom IC-705 over Bluetooth, no cable
// Grove port wiring: G1 = data from the radio, G2 = data to the radio, GND.
// The radios' TTL / CI-V levels need a simple interface (see the Guide).
#pragma once
#include <string>

namespace rig {
void begin();
void tick();
bool connected();
std::string freqMhz();      // "14.074", "" if unknown
std::string mode();         // logbook style: SSB, CW, FM, AM, RTTY, DATA
std::string status();       // a line for the screen: what is connected, or why not
const char* typeName();     // "rigctld", "CI-V", ... or "" when none is set

// Sets the radio to a frequency and (if not empty) a mode such as "SSB",
// "CW", "FT8". False when no radio is connected.
bool tune(double mhz, const std::string& mode);
// Transmit on / off. Uses the Grove PTT line when the Grove port is set to
// "key", else the CAT connection.
bool ptt(bool on);
bool pttAvailable();
// Sends text in CW with the radio's own keyer (CI-V, Kenwood, rigctld).
bool sendCw(const std::string& text);
bool cwAvailable();
}
