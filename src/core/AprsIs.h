// APRS over the internet (APRS-IS) plus the shared station and message lists.
// Off by default: set "APRS-IS" to 1 in Settings. Runs in the background.
// Packets decoded from audio are added to the same lists through ingest().
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aprsis {

enum Source { INTERNET, AUDIO, LORA };

struct Station {
    std::string call, comment;
    double lat = 0, lon = 0;
    char symbolTable = '/', symbol = ' ';
    uint32_t lastHeardMs = 0;
    Source source = INTERNET;   // how it was last heard
    bool viaRadio() const { return source != INTERNET; }
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

// Messages and beacons go out over LoRa when LoRa-APRS is running, otherwise
// over the internet.
bool sendMessage(const std::string& to, const std::string& text, std::string& error);
bool sendBeacon(std::string& error);
std::string beaconPacket(const std::string& path);      // "" without a position
void ingest(const std::string& tnc2, Source source);
bool canSend();                 // online or LoRa-APRS ready
// A complete packet (with our header) to APRS-IS only; false when not logged in.
bool sendRaw(const std::string& tnc2);
// Forwards a packet heard on the radio to APRS-IS (receive-only iGate).
void gate(const std::string& tnc2);

}  // namespace aprsis
