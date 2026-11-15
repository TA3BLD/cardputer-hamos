// Meshtastic-compatible messaging over the LoRa cap. Runs in the background
// from the main loop, so messages arrive while any app is open.
//
// What it does: text messages on one channel (default: the public LongFast
// channel), node names, positions, signal reports.
// What it does not do: relay other nodes' packets (it announces itself as
// CLIENT_MUTE) or direct messages using the newer public-key encryption.
#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace mesh {

struct Node {
    uint32_t id = 0;
    std::string longName, shortName;
    uint32_t lastHeardMs = 0;
    float snr = 0, rssi = 0;
    int hops = 0;               // relays between that node and us on the last packet
    bool hasPosition = false;
    double lat = 0, lon = 0;
};

struct Message {
    uint32_t from = 0, packetId = 0;
    std::string text;
    time_t utc = 0;             // 0 if the clock was not set
    bool mine = false;
    bool direct = false;        // addressed to this node only
    bool sent = false;          // mine: left the antenna
    bool relayed = false;       // mine: heard another node repeat it (it reached the mesh)
};

struct Stats {
    double freqMhz = 0;
    std::string region, preset, channel;
    uint32_t received = 0, sent = 0, otherChannel = 0, undecodable = 0;
    int airtimePercentOfLimit = 0;      // share of this hour's legal transmit time used
    size_t queued = 0;
};

void begin();                   // (re)reads the mesh_* settings and starts the radio
void tick();
bool enabled();
bool ready();
std::string status();           // human readable state or error

bool sendText(const std::string& text, std::string& error);
// To one node (direct message) or BROADCAST; `packetId` gets the id of the packet sent.
constexpr uint32_t BROADCAST = 0xFFFFFFFF;
bool sendTextTo(const std::string& text, uint32_t to, std::string& error, uint32_t* packetId = nullptr);
const std::vector<Message>& messages();
uint32_t messagesAdded();

// Other Meshtastic ports (range test, ...): a handler gets each packet on its
// port addressed to us or to everyone.
using PortHandler = void (*)(uint32_t from, uint32_t to, const std::string& payload, float snr, float rssi);
void onPort(int portnum, PortHandler handler);
uint32_t sendPort(int portnum, const std::string& payload, uint32_t to = BROADCAST);     // 0: not queued      // counts every message since start (the list keeps only the newest)
const std::vector<Node>& nodes();
std::string nodeName(uint32_t id, bool shortName = false);
uint32_t myId();
int unread();
void markRead();
Stats stats();

}  // namespace mesh
