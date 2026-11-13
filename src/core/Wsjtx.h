// Companion for WSJT-X / JTDX running on a PC: receives its decodes, status
// and logged contacts over the network. Off by default ("WSJT-X 0/1" in
// Settings). In WSJT-X set Settings > Reporting > UDP Server to this
// device's address (shown in the FT8 app) and port 2237.
// Contacts logged in WSJT-X are added to the HamOS logbook automatically.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "lib/Dxcc.h"

namespace wsjtxlink {

struct Decode {
    int snr = 0;
    uint32_t df = 0;
    std::string text, from;
    bool cq = false, toMe = false;
    const dxcc::Entity* entity = nullptr;
    bool newEntity = false;     // a country that is not in the logbook yet
};

void begin();
void tick();
bool enabled();
bool listening();
bool active();                  // heard from WSJT-X in the last 30 seconds
const std::vector<Decode>& decodes();       // oldest first
std::string dial();             // "14.074 FT8", "" if unknown
std::string dxCall();
bool transmitting();
uint32_t logged();              // contacts taken over into the logbook

}  // namespace wsjtxlink
