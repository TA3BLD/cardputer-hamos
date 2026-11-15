#include "PhoneApi.h"
#include <Arduino.h>
#include <cstring>
#include <deque>
#include <map>
#include "Config.h"
#include "Mesh.h"
#include "Net.h"
#include "Version.h"
#include "lib/Meshtastic.h"

namespace phoneapi {

using meshtastic::PbReader;
using meshtastic::PbWriter;

namespace {

std::deque<std::string> out;
uint32_t counter = 0;
bool started = false;
uint32_t messagesSeen = 0;             // mesh::messagesAdded() already forwarded
std::map<uint32_t, uint32_t> nodesSent;     // id -> lastHeardMs sent to the app

void queue(int field, const std::string& body) {
    PbWriter w;
    w.fieldVarint(1, ++counter);            // FromRadio.id
    w.fieldBytes(field, body);
    out.push_back(w.out);
    while (out.size() > 200) out.pop_front();
}

void fieldFloat(PbWriter& w, int field, float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    w.fieldFixed32(field, v);
}

uint32_t unixNow() { return net::timeValid() ? (uint32_t)(net::utcMillis() / 1000) : 0; }

std::string user(uint32_t id, const std::string& longName, const std::string& shortName) {
    meshtastic::User u;
    u.id = meshtastic::nodeIdString(id);
    u.longName = longName;
    u.shortName = shortName;
    u.role = 0;
    return meshtastic::encodeUser(u);
}

std::string nodeInfo(const mesh::Node& n) {
    PbWriter w;
    w.fieldVarint(1, n.id);
    w.fieldBytes(2, user(n.id, n.longName.empty() ? mesh::nodeName(n.id) : n.longName,
                         n.shortName.empty() ? mesh::nodeName(n.id, true) : n.shortName));
    if (n.hasPosition) {
        PbWriter p;
        p.fieldFixed32(1, (uint32_t)(int32_t)(n.lat * 1e7));     // sfixed32
        p.fieldFixed32(2, (uint32_t)(int32_t)(n.lon * 1e7));
        w.fieldBytes(3, p.out);
    }
    fieldFloat(w, 4, n.snr);
    uint32_t now = unixNow();
    if (now && n.lastHeardMs) w.fieldFixed32(5, now - (millis() - n.lastHeardMs) / 1000);
    w.fieldVarint(9, (uint64_t)n.hops);
    return w.out;
}

std::string selfInfo() {
    PbWriter w;
    w.fieldVarint(1, mesh::myId());
    w.fieldBytes(2, user(mesh::myId(), mesh::nodeName(mesh::myId()), mesh::nodeName(mesh::myId(), true)));
    return w.out;
}

int regionCode(const std::string& r) { return r == "US" ? 1 : r == "EU_433" ? 2 : r == "EU_868" ? 3 : 0; }
int presetCode(const std::string& p) {
    static const char* names[] = {"LongFast", "LongSlow", "VeryLongSlow", "MediumSlow", "MediumFast",
                                  "ShortSlow", "ShortFast", "LongModerate", "ShortTurbo", "LongTurbo"};
    for (int i = 0; i < 10; i++)
        if (p == names[i]) return i;
    return 0;
}

void sendConfig(uint32_t configId) {
    out.clear();
    mesh::Stats st = mesh::stats();
    {   // my_info
        PbWriter w;
        w.fieldVarint(1, mesh::myId());
        w.fieldVarint(8, 1);
        w.fieldVarint(11, 30200);           // min_app_version
        w.fieldBytes(13, "hamos-cardputer");
        w.fieldVarint(15, mesh::nodes().size() + 1);
        queue(3, w.out);
    }
    {   // metadata
        PbWriter w;
        w.fieldBytes(1, std::string("2.5.0.hamos-") + HAMOS_VERSION);
        w.fieldVarint(2, 23);
        w.fieldVarint(4, 1);                // hasWifi
        w.fieldVarint(5, 1);                // hasBluetooth
        w.fieldVarint(7, 1);                // role CLIENT_MUTE
        w.fieldVarint(9, 255);              // PRIVATE_HW
        queue(13, w.out);
    }
    queue(4, selfInfo());
    nodesSent.clear();
    for (const mesh::Node& n : mesh::nodes()) {
        queue(4, nodeInfo(n));
        nodesSent[n.id] = n.lastHeardMs;
    }
    {   // channel 0 (primary), the others disabled
        std::string psk = config::get("mesh_psk", "AQ==");
        uint8_t raw[32];
        size_t rawLen = meshtastic::base64Decode(psk, raw, sizeof(raw));
        PbWriter cs;
        cs.fieldBytes(2, std::string((const char*)raw, rawLen));
        std::string name = config::get("mesh_channel");
        if (!name.empty() && name != st.preset) cs.fieldBytes(3, name);
        PbWriter c;
        c.fieldVarint(1, 0);
        c.fieldBytes(2, cs.out);
        c.fieldVarint(3, 1);                // PRIMARY
        queue(10, c.out);
        for (int i = 1; i < 8; i++) {
            PbWriter d;
            d.fieldVarint(1, (uint64_t)i);
            d.fieldVarint(3, 0);            // DISABLED
            queue(10, d.out);
        }
    }
    {   // config: lora
        PbWriter lora;
        lora.fieldVarint(1, 1);             // use_preset
        lora.fieldVarint(2, (uint64_t)presetCode(st.preset));
        lora.fieldVarint(7, (uint64_t)regionCode(st.region));
        lora.fieldVarint(8, 3);             // hop_limit
        lora.fieldVarint(9, 1);             // tx_enabled
        lora.fieldVarint(10, (uint64_t)atoi(config::get("lora_power", "22").c_str()));
        PbWriter c;
        c.fieldBytes(6, lora.out);
        queue(5, c.out);
        PbWriter dev;                       // device: role CLIENT_MUTE
        dev.fieldVarint(1, 1);
        PbWriter c2;
        c2.fieldBytes(1, dev.out);
        queue(5, c2.out);
        PbWriter bt;                        // bluetooth: enabled
        bt.fieldVarint(1, 1);
        PbWriter c3;
        c3.fieldBytes(7, bt.out);
        queue(5, c3.out);
    }
    PbWriter done;
    done.fieldVarint(1, ++counter);
    done.fieldVarint(7, configId);
    out.push_back(done.out);
    messagesSeen = mesh::messagesAdded();
}

std::string textPacket(const mesh::Message& m) {
    meshtastic::Data d;
    d.portnum = meshtastic::PORT_TEXT;
    d.payload = m.text;
    PbWriter p;
    p.fieldFixed32(1, m.from);
    p.fieldFixed32(2, m.direct ? mesh::myId() : mesh::BROADCAST);
    p.fieldBytes(4, meshtastic::encodeData(d));
    p.fieldFixed32(6, m.packetId);
    if (m.utc) p.fieldFixed32(7, (uint32_t)m.utc);
    return p.out;
}

// Tells the app its packet went out (a Routing message with no error).
void ack(uint32_t requestId) {
    meshtastic::Data d;
    d.portnum = meshtastic::PORT_ROUTING;
    PbWriter routing;
    routing.fieldVarint(3, 0);              // error_reason NONE
    d.payload = routing.out;
    d.requestId = requestId;
    PbWriter p;
    p.fieldFixed32(1, mesh::myId());
    p.fieldFixed32(2, mesh::myId());
    p.fieldBytes(4, meshtastic::encodeData(d));
    p.fieldFixed32(6, esp_random());
    queue(2, p.out);
}

void fromApp(const std::string& packet) {
    PbReader r(packet);
    uint32_t to = mesh::BROADCAST, id = 0;
    std::string decoded;
    while (r.next()) {
        if (r.field == 2 && r.wire == 5) to = (uint32_t)r.value;
        else if (r.field == 4 && r.wire == 2) decoded = r.bytes;
        else if (r.field == 6 && r.wire == 5) id = (uint32_t)r.value;
    }
    meshtastic::Data d;
    if (decoded.empty() || !meshtastic::decodeData(decoded, d)) return;
    if (d.portnum != meshtastic::PORT_TEXT) return;         // positions, admin: not supported
    std::string err;
    if (mesh::sendTextTo(d.payload, to, err) && id) ack(id);
}

}  // namespace

void handleToRadio(const std::string& msg) {
    PbReader r(msg);
    while (r.next()) {
        if (r.field == 3 && r.wire == 0) {              // want_config_id
            started = true;
            sendConfig((uint32_t)r.value);
        } else if (r.field == 1 && r.wire == 2) {       // packet
            fromApp(r.bytes);
        } else if (r.field == 4) {                      // disconnect
            reset();
        }
    }
}

bool nextFromRadio(std::string& m) {
    if (out.empty()) return false;
    m = out.front();
    out.pop_front();
    return true;
}

uint32_t fromNum() { return counter; }
bool active() { return started; }

void reset() {
    started = false;
    out.clear();
}

void tick() {
    if (!started) return;
    const std::vector<mesh::Message>& msgs = mesh::messages();
    uint32_t added = mesh::messagesAdded();
    size_t fresh = added - messagesSeen;
    if (fresh > msgs.size()) fresh = msgs.size();
    for (size_t i = msgs.size() - fresh; i < msgs.size(); i++)
        if (!msgs[i].mine) queue(2, textPacket(msgs[i]));       // the app keeps its own copy of what it sent
    messagesSeen = added;
    for (const mesh::Node& n : mesh::nodes()) {
        auto it = nodesSent.find(n.id);
        if (it != nodesSent.end() && it->second == n.lastHeardMs) continue;
        nodesSent[n.id] = n.lastHeardMs;
        queue(4, nodeInfo(n));
    }
}

}  // namespace phoneapi
