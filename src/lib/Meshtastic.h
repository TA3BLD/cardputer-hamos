// Meshtastic on-air format: packet header, AES-CTR channel encryption, the
// protobuf messages HamOS uses, and radio parameters. Pure C++, host-testable.
// Reference: https://meshtastic.org/docs/overview/mesh-algo/ and the
// meshtastic/protobufs repository.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace meshtastic {

constexpr uint32_t BROADCAST = 0xFFFFFFFF;
constexpr size_t HEADER_LEN = 16;
constexpr size_t MAX_PACKET = 255;
constexpr size_t MAX_TEXT = 200;        // bytes of text that safely fit one packet
constexpr uint8_t SYNC_WORD = 0x2B;
constexpr uint16_t PREAMBLE = 16;

enum Port { PORT_TEXT = 1, PORT_POSITION = 3, PORT_NODEINFO = 4, PORT_ROUTING = 5 };

// ---------------------------------------------------------------- header ----

struct Header {
    uint32_t to = BROADCAST, from = 0, id = 0;
    uint8_t hopLimit = 3, hopStart = 3;
    bool wantAck = false, viaMqtt = false;
    uint8_t channel = 0;                // channel hash
    uint8_t nextHop = 0, relayNode = 0;
};

inline void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline void packHeader(const Header& h, uint8_t* out) {
    put32(out, h.to);
    put32(out + 4, h.from);
    put32(out + 8, h.id);
    out[12] = (uint8_t)((h.hopLimit & 7) | (h.wantAck ? 0x08 : 0) | (h.viaMqtt ? 0x10 : 0) | ((h.hopStart & 7) << 5));
    out[13] = h.channel;
    out[14] = h.nextHop;
    out[15] = h.relayNode;
}

inline bool unpackHeader(const uint8_t* in, size_t len, Header& h) {
    if (len < HEADER_LEN) return false;
    h.to = get32(in);
    h.from = get32(in + 4);
    h.id = get32(in + 8);
    h.hopLimit = in[12] & 7;
    h.wantAck = in[12] & 0x08;
    h.viaMqtt = in[12] & 0x10;
    h.hopStart = (in[12] >> 5) & 7;
    h.channel = in[13];
    h.nextHop = in[14];
    h.relayNode = in[15];
    return true;
}

// ------------------------------------------------------------ encryption ----

// AES block cipher supplied by the platform (mbedtls on the device).
struct BlockCipher {
    virtual ~BlockCipher() {}
    virtual void encryptBlock(const uint8_t in[16], uint8_t out[16]) = 0;
};

// AES-CTR, encrypts or decrypts in place. The nonce is the packet id (64 bit
// little endian) followed by the sender node number and a 32 bit block counter.
inline void cryptCtr(BlockCipher& aes, uint32_t from, uint32_t packetId, uint8_t* data, size_t len) {
    uint8_t counter[16] = {0}, stream[16];
    put32(counter, packetId);
    put32(counter + 8, from);
    for (size_t off = 0; off < len; off += 16) {
        aes.encryptBlock(counter, stream);
        for (size_t i = 0; i < 16 && off + i < len; i++) data[off + i] ^= stream[i];
        for (int i = 15; i >= 0 && ++counter[i] == 0; i--) {}   // big-endian increment
    }
}

constexpr uint8_t kDefaultKey[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

inline size_t base64Decode(const std::string& in, uint8_t* out, size_t max) {
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else continue;                   // padding, whitespace
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n < max) out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return n;
}

// Expands a channel PSK as written in the Meshtastic apps (base64). Returns the
// key length: 16 or 32, 0 for "no encryption", -1 if the PSK is not usable.
//   "AQ==" (one byte 1) is the well-known default key; one byte N>1 is the
//   default key with its last byte increased by N-1; "" or one byte 0 is none.
inline int expandPsk(const std::string& b64, uint8_t key[32]) {
    uint8_t raw[33];
    size_t n = base64Decode(b64, raw, sizeof(raw));
    if (n == 0 || (n == 1 && raw[0] == 0)) return 0;
    if (n == 1) {
        memcpy(key, kDefaultKey, 16);
        key[15] = (uint8_t)(key[15] + raw[0] - 1);
        return 16;
    }
    if (n == 16 || n == 32) {
        memcpy(key, raw, n);
        return (int)n;
    }
    return -1;
}

inline uint8_t channelHash(const std::string& name, const uint8_t* key, size_t keyLen) {
    uint8_t h = 0;
    for (char c : name) h ^= (uint8_t)c;
    for (size_t i = 0; i < keyLen; i++) h ^= key[i];
    return h;
}

// -------------------------------------------------------------- protobuf ----

class PbWriter {
public:
    std::string out;
    void varint(uint64_t v) {
        while (v >= 0x80) { out += (char)(v | 0x80); v >>= 7; }
        out += (char)v;
    }
    void tag(int field, int wire) { varint(((uint64_t)field << 3) | (uint64_t)wire); }
    void fieldVarint(int field, uint64_t v) { tag(field, 0); varint(v); }
    void fieldFixed32(int field, uint32_t v) {
        tag(field, 5);
        uint8_t b[4];
        put32(b, v);
        out.append((const char*)b, 4);
    }
    void fieldBytes(int field, const std::string& s) { tag(field, 2); varint(s.size()); out += s; }
};

class PbReader {
public:
    explicit PbReader(const std::string& s) : p_((const uint8_t*)s.data()), end_(p_ + s.size()) {}
    int field = 0, wire = 0;
    uint64_t value = 0;      // varint / fixed32 / fixed64 fields
    std::string bytes;       // length-delimited fields

    // Reads the next field. Returns false at the end or on malformed input.
    bool next() {
        uint64_t key;
        if (p_ >= end_ || !varint(key)) return false;
        field = (int)(key >> 3);
        wire = (int)(key & 7);
        if (wire == 0) return varint(value);
        if (wire == 5) return fixed(4);
        if (wire == 1) return fixed(8);
        if (wire == 2) {
            uint64_t len;
            if (!varint(len) || len > (uint64_t)(end_ - p_)) return fail();
            bytes.assign((const char*)p_, (size_t)len);
            p_ += len;
            return true;
        }
        return fail();
    }
    bool ok() const { return !bad_; }   // false if parsing stopped on an error

private:
    bool fail() { bad_ = true; return false; }
    bool varint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p_ >= end_) return fail();
            uint8_t b = *p_++;
            v |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) return true;
        }
        return fail();
    }
    bool fixed(int n) {
        if (end_ - p_ < n) return fail();
        value = 0;
        for (int i = 0; i < n; i++) value |= (uint64_t)p_[i] << (8 * i);
        p_ += n;
        return true;
    }
    const uint8_t* p_;
    const uint8_t* end_;
    bool bad_ = false;
};

// meshtastic.Data: the decrypted payload of every packet.
struct Data {
    int portnum = 0;
    std::string payload;
    bool wantResponse = false;
    uint32_t requestId = 0, replyId = 0, emoji = 0;
};

inline std::string encodeData(const Data& d) {
    PbWriter w;
    w.fieldVarint(1, (uint64_t)d.portnum);
    w.fieldBytes(2, d.payload);
    if (d.wantResponse) w.fieldVarint(3, 1);
    if (d.requestId) w.fieldFixed32(6, d.requestId);
    if (d.replyId) w.fieldFixed32(7, d.replyId);
    if (d.emoji) w.fieldFixed32(8, d.emoji);
    return w.out;
}

inline bool decodeData(const std::string& in, Data& d) {
    d = Data();
    PbReader r(in);
    bool sawPort = false;
    while (r.next()) {
        if (r.field == 1 && r.wire == 0) { d.portnum = (int)r.value; sawPort = true; }
        else if (r.field == 2 && r.wire == 2) d.payload = r.bytes;
        else if (r.field == 3 && r.wire == 0) d.wantResponse = r.value != 0;
        else if (r.field == 6 && r.wire == 5) d.requestId = (uint32_t)r.value;
        else if (r.field == 7 && r.wire == 5) d.replyId = (uint32_t)r.value;
        else if (r.field == 8 && r.wire == 5) d.emoji = (uint32_t)r.value;
    }
    // A wrong key decrypts to noise; requiring a clean parse and a port filters that out.
    return r.ok() && sawPort && d.portnum > 0 && d.portnum < 512;
}

// meshtastic.User: who a node is (sent on the NODEINFO port).
struct User {
    std::string id, longName, shortName;
    int hwModel = 255;      // PRIVATE_HW
    int role = 1;           // CLIENT_MUTE: HamOS does not relay other nodes' packets
    bool licensed = false;
};

inline std::string encodeUser(const User& u) {
    PbWriter w;
    w.fieldBytes(1, u.id);
    w.fieldBytes(2, u.longName);
    w.fieldBytes(3, u.shortName);
    w.fieldVarint(5, (uint64_t)u.hwModel);
    if (u.licensed) w.fieldVarint(6, 1);
    if (u.role) w.fieldVarint(7, (uint64_t)u.role);
    return w.out;
}

inline bool decodeUser(const std::string& in, User& u) {
    u = User();
    u.hwModel = 0;
    u.role = 0;
    PbReader r(in);
    while (r.next()) {
        if (r.field == 1 && r.wire == 2) u.id = r.bytes;
        else if (r.field == 2 && r.wire == 2) u.longName = r.bytes;
        else if (r.field == 3 && r.wire == 2) u.shortName = r.bytes;
        else if (r.field == 5 && r.wire == 0) u.hwModel = (int)r.value;
        else if (r.field == 6 && r.wire == 0) u.licensed = r.value != 0;
        else if (r.field == 7 && r.wire == 0) u.role = (int)r.value;
    }
    return r.ok();
}

// meshtastic.Position (the fields HamOS uses).
struct Position {
    bool hasLatLon = false;
    double lat = 0, lon = 0;
    int32_t altitude = 0;
    uint32_t time = 0;          // unix seconds
    uint32_t precisionBits = 0;
};

// Reduces a coordinate to `bits` of precision the way the firmware does
// (13 bits is roughly a 3 km square), so the exact location is not broadcast.
inline int32_t reducePrecision(int32_t v, uint32_t bits) {
    if (bits == 0 || bits >= 32) return v;
    uint32_t u = (uint32_t)v & (0xFFFFFFFFu << (32 - bits));
    u += 1u << (31 - bits);     // centre of the square
    return (int32_t)u;
}

inline std::string encodePosition(const Position& p) {
    PbWriter w;
    if (p.hasLatLon) {
        int32_t lat = (int32_t)std::lround(p.lat * 1e7), lon = (int32_t)std::lround(p.lon * 1e7);
        w.fieldFixed32(1, (uint32_t)reducePrecision(lat, p.precisionBits));
        w.fieldFixed32(2, (uint32_t)reducePrecision(lon, p.precisionBits));
    }
    if (p.altitude) w.fieldVarint(3, (uint64_t)(int64_t)p.altitude);
    if (p.time) w.fieldFixed32(4, p.time);
    if (p.precisionBits) w.fieldVarint(23, p.precisionBits);
    return w.out;
}

inline bool decodePosition(const std::string& in, Position& p) {
    p = Position();
    bool lat = false, lon = false;
    PbReader r(in);
    while (r.next()) {
        if (r.field == 1 && r.wire == 5) { p.lat = (int32_t)(uint32_t)r.value / 1e7; lat = true; }
        else if (r.field == 2 && r.wire == 5) { p.lon = (int32_t)(uint32_t)r.value / 1e7; lon = true; }
        else if (r.field == 3 && r.wire == 0) p.altitude = (int32_t)r.value;
        else if (r.field == 4 && r.wire == 5) p.time = (uint32_t)r.value;
        else if (r.field == 23 && r.wire == 0) p.precisionBits = (uint32_t)r.value;
    }
    p.hasLatLon = lat && lon;
    return r.ok();
}

inline std::string nodeIdString(uint32_t node) {
    char buf[12];
    snprintf(buf, sizeof(buf), "!%08x", (unsigned)node);
    return buf;
}

// ------------------------------------------------------- radio settings ----

struct Region {
    const char* name;
    double startMhz, endMhz;
    int dutyCyclePercent;   // 100 = no limit
    int maxPowerDbm;
};

inline const Region* findRegion(const std::string& name) {
    static const Region regions[] = {
        {"EU_868", 869.4, 869.65, 10, 27},
        {"EU_433", 433.0, 434.0, 10, 10},
        {"US", 902.0, 928.0, 100, 30},
    };
    for (const Region& r : regions)
        if (name == r.name) return &r;
    return nullptr;
}

struct Preset {
    const char* name;       // also the default channel name
    double bwKhz;
    int sf, cr;             // cr: coding rate denominator, 5..8
};

inline const Preset* findPreset(const std::string& name) {
    static const Preset presets[] = {
        {"LongFast", 250, 11, 5},   {"LongSlow", 125, 12, 8},  {"LongModerate", 125, 11, 8},
        {"LongTurbo", 500, 11, 8},  {"MediumSlow", 250, 10, 5}, {"MediumFast", 250, 9, 5},
        {"ShortSlow", 250, 8, 5},   {"ShortFast", 250, 7, 5},  {"ShortTurbo", 500, 7, 5},
    };
    for (const Preset& p : presets)
        if (name == p.name) return &p;
    return nullptr;
}

// Centre frequency the firmware picks for a channel name (frequency slot = hash of the name).
inline double channelFrequencyMhz(const Region& region, const Preset& preset, const std::string& channelName) {
    double bwMhz = preset.bwKhz / 1000.0;
    uint32_t slots = (uint32_t)std::floor((region.endMhz - region.startMhz) / bwMhz + 1e-9);
    if (slots == 0) slots = 1;
    uint32_t hash = 5381;                           // djb2
    for (char c : channelName) hash = ((hash << 5) + hash) + (uint8_t)c;
    return region.startMhz + bwMhz / 2 + (hash % slots) * bwMhz;
}

// LoRa time on air in milliseconds (explicit header, CRC on).
inline double timeOnAirMs(const Preset& preset, size_t packetLen) {
    double tSym = std::pow(2.0, preset.sf) / preset.bwKhz;            // ms
    int lowDataRate = tSym > 16.0 ? 1 : 0;
    double bits = 8.0 * packetLen - 4.0 * preset.sf + 28 + 16;
    double symbols = std::ceil(bits / (4.0 * (preset.sf - 2 * lowDataRate))) * preset.cr;
    if (symbols < 0) symbols = 0;
    return (PREAMBLE + 4.25 + 8 + symbols) * tSym;
}

}  // namespace meshtastic
