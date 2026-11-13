// APRS over the internet (APRS-IS) plus the shared station and message lists.
// Off by default: set "APRS-IS" to 1 in Settings. Runs in the background.
// Packets decoded from audio are added to the same lists through ingest().
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aprsis {

struct Station {
    std::string call, comment;
    double lat = 0, lon = 0;
    char symbol = ' ';
    uint32_t lastHeardMs = 0;
    bool viaRadio = false;      // decoded from audio rather than received from the internet
};

struct Message {
    std::string from, to, text, id;
    bool mine = false, acked = false;
};

void begin();                   // (re)reads the aprs_* settings
void tick();
bool enabled();
bool online();
std::string status();
std::string myCall();           // callsign with SSID, "" if no callsign is set

const std::vector<Station>& stations();
const std::vector<Message>& messages();
int unread();
void markRead();

bool sendMessage(const std::string& to, const std::string& text, std::string& error);
bool sendBeacon(std::string& error);
std::string beaconPacket(const std::string& path);      // "" without a position
void ingest(const std::string& tnc2, bool viaRadio);

}  // namespace aprsis
