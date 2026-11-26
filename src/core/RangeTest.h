// Range test, compatible with Meshtastic's Range Test module (port 66):
// one device sends "seq 1", "seq 2", ... at a fixed interval; the other notes
// every one it hears with signal, its own position and the distance. Walk or
// drive with the receiver and see on the map how far the signal reaches.
// Every reception is also written to /HamOS/rangetest.csv.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rangetest {

enum class Role { Off, Send, Receive };

struct Hit {
    uint32_t from = 0, seq = 0;
    float snr = 0, rssi = 0;
    uint32_t ms = 0;
    bool havePos = false;
    double lat = 0, lon = 0;        // where we were
    double km = -1;                 // to the sender, when its position is known
};

void begin();                       // registers the port handler
void setRole(Role r);
Role role();
void tick();
uint32_t sentCount();
int intervalSeconds();
const std::vector<Hit>& hits();
int lost();                         // gaps in the sequence numbers heard
double bestKm();

}  // namespace rangetest
