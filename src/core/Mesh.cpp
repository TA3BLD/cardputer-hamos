#include "Mesh.h"
#include <M5Cardputer.h>
#include <esp_random.h>
#include <deque>
#include "Config.h"
#include "Lang.h"
#include "Gps.h"
#include "Lora.h"
#include "Storage.h"
#include "lib/Meshtastic.h"
#include "mbedtls/aes.h"

namespace mesh {

using namespace meshtastic;

namespace {

struct MbedAes : BlockCipher {
    mbedtls_aes_context ctx;
    bool initialised = false;
    void setKey(const uint8_t* key, int len) {
        if (initialised) mbedtls_aes_free(&ctx);
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_enc(&ctx, key, (unsigned)len * 8);
        initialised = true;
    }
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) override {
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
    }
};

struct Outgoing {
    std::string packet;         // header + encrypted payload
    uint32_t packetId;
};

constexpr size_t MAX_MESSAGES = 60, MAX_NODES = 64, MAX_QUEUE = 8;
constexpr uint32_t NODEINFO_INTERVAL_MS = 3UL * 3600 * 1000;
constexpr uint32_t POSITION_INTERVAL_MS = 30UL * 60 * 1000;
constexpr const char* kLogPath = "/HamOS/mesh.txt";

bool isEnabled = false, isReady = false;
std::string statusText = "off";
uint32_t nodeId = 0;
MbedAes aes;
int keyLen = 0;
uint8_t chanHash = 0;
const Region* region = nullptr;
const Preset* preset = nullptr;
std::string channelName;
double freqMhz = 0;

std::vector<Message> msgs;
std::vector<Node> nodeList;
std::deque<Outgoing> queue;
int unreadCount = 0;
uint32_t rxCount = 0, txCount = 0, otherCount = 0, badCount = 0;
uint32_t lastNodeInfoMs = 0, lastPositionMs = 0, lastNodeInfoReplyMs = 0, lastRxMs = 0, backoffMs = 0;
bool nodeInfoSent = false, positionSent = false;

// Recently seen packets (the mesh repeats every packet several times).
struct Seen { uint32_t from, id; };
Seen seen[32];
size_t seenNext = 0;

// Transmit time used, for the regional duty cycle limit.
struct Air { uint32_t atMs; uint32_t ms; };
std::deque<Air> airLog;

uint32_t airtimeLastHourMs() {
    uint32_t now = millis(), total = 0;
    while (!airLog.empty() && now - airLog.front().atMs > 3600000UL) airLog.pop_front();
    for (const Air& a : airLog) total += a.ms;
    return total;
}

uint32_t airtimeLimitMs() { return region ? (uint32_t)region->dutyCyclePercent * 36000UL : 0; }

bool alreadySeen(uint32_t from, uint32_t id) {
    for (const Seen& s : seen)
        if (s.from == from && s.id == id) return true;
    seen[seenNext] = {from, id};
    seenNext = (seenNext + 1) % (sizeof(seen) / sizeof(seen[0]));
    return false;
}

Node& touchNode(uint32_t id) {
    for (Node& n : nodeList)
        if (n.id == id) return n;
    if (nodeList.size() >= MAX_NODES) {         // forget the node heard longest ago
        size_t oldest = 0;
        for (size_t i = 1; i < nodeList.size(); i++)
            if (millis() - nodeList[i].lastHeardMs > millis() - nodeList[oldest].lastHeardMs) oldest = i;
        nodeList.erase(nodeList.begin() + (long)oldest);
    }
    Node n;
    n.id = id;
    nodeList.push_back(n);
    return nodeList.back();
}

uint32_t addedCount = 0;
struct Hook {
    int port;
    PortHandler handler;
};
std::vector<Hook> hooks;

void addMessage(const Message& m) {
    addedCount++;
    if (msgs.size() >= MAX_MESSAGES) msgs.erase(msgs.begin());
    msgs.push_back(m);
    char stamp[24] = "----------- --:--";
    if (m.utc) {
        struct tm t;
        gmtime_r(&m.utc, &t);
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &t);
    }
    storage::appendFile(kLogPath, std::string(stamp) + " " + nodeName(m.from) + ": " + m.text + "\n");
}

time_t nowUtc() {
    time_t t = time(nullptr);
    return t > 1700000000 ? t : 0;
}

// Encrypts `data` and queues it. Returns the packet id, 0 if the queue is full.
uint32_t enqueue(const Data& data, uint32_t to, bool wantAck) {
    if (queue.size() >= MAX_QUEUE) return 0;
    Header h;
    h.to = to;
    h.from = nodeId;
    do { h.id = esp_random(); } while (h.id == 0);
    h.wantAck = wantAck;
    h.channel = chanHash;
    std::string body = encodeData(data);
    if (HEADER_LEN + body.size() > MAX_PACKET) return 0;
    if (keyLen) cryptCtr(aes, h.from, h.id, (uint8_t*)&body[0], body.size());
    uint8_t head[HEADER_LEN];
    packHeader(h, head);
    queue.push_back({std::string((const char*)head, HEADER_LEN) + body, h.id});
    return h.id;
}

User myUser() {
    User u;
    u.id = nodeIdString(nodeId);
    std::string call = config::get("callsign");
    char tail[8];
    snprintf(tail, sizeof(tail), "%04x", (unsigned)(nodeId & 0xFFFF));
    u.longName = config::get("mesh_name", call.empty() ? std::string("HamOS ") + tail : call);
    u.shortName = config::get("mesh_short", call.size() >= 4 ? call.substr(call.size() - 4) : std::string(tail));
    if (u.longName.size() > 36) u.longName.resize(36);
    if (u.shortName.size() > 4) u.shortName.resize(4);
    return u;
}

void queueNodeInfo(uint32_t to) {
    Data d;
    d.portnum = PORT_NODEINFO;
    d.payload = encodeUser(myUser());
    enqueue(d, to, false);
}

void queuePosition() {
    Position p;
    p.hasLatLon = true;
    p.lat = gps::fix().lat;
    p.lon = gps::fix().lon;
    p.altitude = (int32_t)gps::fix().altitudeM;
    p.time = (uint32_t)nowUtc();
    p.precisionBits = 13;       // about 3 km: enough for a map, not a home address
    Data d;
    d.portnum = PORT_POSITION;
    d.payload = encodePosition(p);
    enqueue(d, BROADCAST, false);
}

void onPacket(const uint8_t* data, size_t len, float rssi, float snr) {
    Header h;
    if (!unpackHeader(data, len, h)) return;
    lastRxMs = millis();
    backoffMs = 400 + esp_random() % 1200;      // let the channel settle before we transmit
    if (h.from == nodeId) {                     // someone repeating our own packet: it got out
        for (Message& m : msgs)
            if (m.mine && m.packetId == h.id) m.relayed = true;
        return;
    }
    if (alreadySeen(h.from, h.id)) return;
    if (h.channel != chanHash) { otherCount++; return; }

    std::string body((const char*)data + HEADER_LEN, len - HEADER_LEN);
    if (keyLen) cryptCtr(aes, h.from, h.id, (uint8_t*)&body[0], body.size());
    Data d;
    if (!decodeData(body, d)) { badCount++; return; }
    rxCount++;

    Node& node = touchNode(h.from);
    node.lastHeardMs = millis();
    node.snr = snr;
    node.rssi = rssi;
    node.hops = h.hopStart >= h.hopLimit ? h.hopStart - h.hopLimit : 0;

    bool forMe = h.to == nodeId;
    if (d.portnum == PORT_TEXT && (forMe || h.to == BROADCAST)) {
        Message m;
        m.from = h.from;
        m.packetId = h.id;
        m.text = d.payload;
        m.utc = nowUtc();
        m.direct = forMe;
        addMessage(m);
        unreadCount++;
        if (config::get("mesh_beep", "1") == "1") M5Cardputer.Speaker.tone(1800, 60);
    } else if (d.portnum == PORT_NODEINFO) {
        User u;
        if (decodeUser(d.payload, u)) {
            node.longName = u.longName;
            node.shortName = u.shortName;
        }
        if (d.wantResponse && millis() - lastNodeInfoReplyMs > 60000) {
            lastNodeInfoReplyMs = millis();
            queueNodeInfo(h.from);
        }
    } else if (d.portnum == PORT_POSITION) {
        Position p;
        if (decodePosition(d.payload, p) && p.hasLatLon) {
            node.hasPosition = true;
            node.lat = p.lat;
            node.lon = p.lon;
        }
    }
    if (forMe || h.to == BROADCAST)
        for (const Hook& k : hooks)
            if (k.port == d.portnum) k.handler(h.from, h.to, d.payload, snr, rssi);
    if (forMe && h.wantAck) {                   // confirm receipt to the sender
        Data ack;
        ack.portnum = PORT_ROUTING;
        ack.payload = std::string("\x18\x00", 2);   // Routing.error_reason = NONE
        ack.requestId = h.id;
        enqueue(ack, h.from, false);
    }
}

}  // namespace

void begin() {
    isReady = false;
    queue.clear();
    isEnabled = config::get("mesh_on", "1") == "1" && config::get("lora_mode", "mesh") != "aprs";
    if (!isEnabled) {
        statusText = config::get("lora_mode", "mesh") == "aprs" ? "LoRa radio is in APRS mode (Settings)" : "Mesh is switched off in Settings";
        return;
    }

    region = findRegion(config::get("mesh_region", "EU_868"));
    preset = findPreset(config::get("mesh_preset", "LongFast"));
    if (!region) { statusText = T("Unknown mesh_region (EU_868, EU_433, US)"); return; }
    if (!preset) { statusText = T("Unknown mesh_preset (e.g. LongFast)"); return; }
    channelName = config::get("mesh_channel", preset->name);

    uint8_t key[32];
    keyLen = expandPsk(config::get("mesh_psk", "AQ=="), key);
    if (keyLen < 0) { statusText = T("mesh_psk is not a valid key"); return; }
    if (keyLen) aes.setKey(key, keyLen);
    chanHash = channelHash(channelName, key, (size_t)keyLen);

    // Node number: the last four bytes of the factory MAC address, like the official firmware.
    uint64_t mac = ESP.getEfuseMac();
    nodeId = ((uint32_t)((mac >> 16) & 0xFF) << 24) | ((uint32_t)((mac >> 24) & 0xFF) << 16) |
             ((uint32_t)((mac >> 32) & 0xFF) << 8) | (uint32_t)((mac >> 40) & 0xFF);

    lora::Settings s;
    s.freqMhz = freqMhz = channelFrequencyMhz(*region, *preset, channelName);
    s.bwKhz = preset->bwKhz;
    s.sf = preset->sf;
    s.cr = preset->cr;
    s.syncWord = SYNC_WORD;
    s.preamble = PREAMBLE;
    int power = atoi(config::get("lora_power", "22").c_str());
    if (power > region->maxPowerDbm) power = region->maxPowerDbm;
    if (power > 22) power = 22;                 // SX1262 maximum
    if (power < -9) power = -9;
    s.powerDbm = power;
    lora::onReceive(onPacket);
    if (!lora::begin(s)) { statusText = lora::error(); return; }
    isReady = true;
    statusText = "ready";
    nodeInfoSent = positionSent = false;
}

void tick() {
    if (!isReady) return;
    lora::tick();
    uint32_t now = millis();

    if (!nodeInfoSent || now - lastNodeInfoMs > NODEINFO_INTERVAL_MS) {
        queueNodeInfo(BROADCAST);
        nodeInfoSent = true;
        lastNodeInfoMs = now;
    }
    if (config::get("mesh_pos", "0") == "1" && gps::hasFix() &&
        (!positionSent || now - lastPositionMs > POSITION_INTERVAL_MS)) {
        queuePosition();
        positionSent = true;
        lastPositionMs = now;
    }

    if (queue.empty() || lora::busy() || now - lastRxMs < backoffMs) return;
    const Outgoing& out = queue.front();
    uint32_t air = (uint32_t)timeOnAirMs(*preset, out.packet.size());
    if (airtimeLastHourMs() + air > airtimeLimitMs()) return;    // wait: legal airtime used up
    if (!lora::send((const uint8_t*)out.packet.data(), out.packet.size())) return;
    airLog.push_back({now, air});
    txCount++;
    for (Message& m : msgs)
        if (m.mine && m.packetId == out.packetId) m.sent = true;
    queue.pop_front();
    lastRxMs = now;
    backoffMs = air + 300;                      // do not queue a second packet on top of this one
}

bool enabled() { return isEnabled; }
bool ready() { return isReady; }
std::string status() { return statusText; }
uint32_t myId() { return nodeId; }
const std::vector<Message>& messages() { return msgs; }
const std::vector<Node>& nodes() { return nodeList; }
int unread() { return unreadCount; }
void markRead() { unreadCount = 0; }

std::string nodeName(uint32_t id, bool shortName) {
    if (id == nodeId) return shortName ? myUser().shortName : myUser().longName;
    for (const Node& n : nodeList)
        if (n.id == id) {
            const std::string& name = shortName ? n.shortName : n.longName;
            if (!name.empty()) return name;
        }
    char buf[12];
    snprintf(buf, sizeof(buf), shortName ? "%04x" : "!%08x", (unsigned)(shortName ? id & 0xFFFF : id));
    return buf;
}

bool sendText(const std::string& text, std::string& error) { return sendTextTo(text, BROADCAST, error); }
uint32_t messagesAdded() { return addedCount; }

void onPort(int portnum, PortHandler handler) {
    for (Hook& k : hooks)
        if (k.port == portnum) { k.handler = handler; return; }
    hooks.push_back({portnum, handler});
}

uint32_t sendPort(int portnum, const std::string& payload, uint32_t to) {
    if (!isReady) return 0;
    Data d;
    d.portnum = portnum;
    d.payload = payload;
    return enqueue(d, to, false);
}

bool sendTextTo(const std::string& text, uint32_t to, std::string& error, uint32_t* packetId) {
    if (!isReady) { error = statusText; return false; }
    if (text.empty()) { error = T("Nothing to send"); return false; }
    if (text.size() > MAX_TEXT) { error = T("Message too long"); return false; }
    Data d;
    d.portnum = PORT_TEXT;
    d.payload = text;
    uint32_t id = enqueue(d, to, to != BROADCAST);
    if (!id) { error = T("Send queue is full"); return false; }
    if (packetId) *packetId = id;
    Message m;
    m.from = nodeId;
    m.packetId = id;
    m.text = text;
    m.utc = nowUtc();
    m.mine = true;
    m.direct = to != BROADCAST;
    addMessage(m);
    return true;
}

Stats stats() {
    Stats s;
    s.freqMhz = freqMhz;
    s.region = region ? region->name : "";
    s.preset = preset ? preset->name : "";
    s.channel = channelName;
    s.received = rxCount;
    s.sent = txCount;
    s.otherChannel = otherCount;
    s.undecodable = badCount;
    uint32_t limit = airtimeLimitMs();
    s.airtimePercentOfLimit = limit ? (int)(100.0 * airtimeLastHourMs() / limit) : 0;
    s.queued = queue.size();
    return s;
}

}  // namespace mesh
