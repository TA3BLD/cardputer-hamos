// Runs the real HamOS code on a PC with simulated hardware: presses keys from
// a script, feeds fake GPS / LoRa / network data, records what would be drawn
// and checks a few end-to-end results. See run.sh.
#include "main.cpp"

#include <RadioLib.h>
#include "core/Log.h"
#include "lib/Meshtastic.h"
#include "mbedtls/aes.h"

uint32_t sim_ms = 1000;
static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { fprintf(stderr, "SIM CHECK FAILED line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

// ---- simulated GNSS receiver: Izmir, 8 satellites ----
static std::string nmeaLine(const std::string& body) {
    uint8_t sum = 0;
    for (char c : body) sum ^= (uint8_t)c;
    char tail[8];
    snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
    return "$" + body + tail;
}
static void feedGps() {
    Serial1.inject(nmeaLine("GNGGA,110900.00,3825.422,N,02708.568,E,1,08,1.1,121.0,M,36.0,M,,"));
    Serial1.inject(nmeaLine("GNRMC,110900.00,A,3825.422,N,02708.568,E,0.4,271.0,131126,,,A"));
}

static void run(int ms = 60) {
    static uint32_t lastGps = 0;
    for (int t = 0; t < ms; t += 10) {
        sim_ms += 10;
        if (sim_ms - lastGps >= 1000) { lastGps = sim_ms; feedGps(); }
        loop();
    }
}

// ---- keyboard ----
static void press(const Keyboard_Class::KeysState& s) {
    M5Cardputer.Keyboard.inject(s);
    run();
}
static void key(char c) { Keyboard_Class::KeysState s; s.word.push_back(c); press(s); }
static void type(const char* text) { for (const char* p = text; *p; p++) key(*p); }
static void enter() { Keyboard_Class::KeysState s; s.enter = true; press(s); }
static void tab() { Keyboard_Class::KeysState s; s.tab = true; press(s); }
static void del(int n = 1) { while (n--) { Keyboard_Class::KeysState s; s.del = true; press(s); } }
static void back() { key('`'); }
static void shot(const char* name) { run(1100); printf("S %s\n", name); }
static void openNext() { key('/'); enter(); }   // next launcher tile

// ---- simulated mesh neighbours ----
struct SimAes : meshtastic::BlockCipher {
    mbedtls_aes_context ctx;
    SimAes() { mbedtls_aes_setkey_enc(&ctx, meshtastic::kDefaultKey, 128); }
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) override { mbedtls_aes_crypt_ecb(&ctx, 1, in, out); }
};
static SimAes simAes;

static std::string openPacket(const std::string& packet, meshtastic::Header& h) {
    meshtastic::unpackHeader((const uint8_t*)packet.data(), packet.size(), h);
    std::string body = packet.substr(meshtastic::HEADER_LEN);
    meshtastic::cryptCtr(simAes, h.from, h.id, (uint8_t*)&body[0], body.size());
    return body;
}

static void fromNode(uint32_t node, int port, const std::string& payload, int hopsAway = 0) {
    static uint32_t nextId = 0x1000;
    meshtastic::Header h;
    h.from = node;
    h.id = nextId++;
    h.hopStart = 3;
    h.hopLimit = (uint8_t)(3 - hopsAway);
    h.channel = 0x08;
    meshtastic::Data d;
    d.portnum = port;
    d.payload = payload;
    std::string body = meshtastic::encodeData(d);
    meshtastic::cryptCtr(simAes, h.from, h.id, (uint8_t*)&body[0], body.size());
    uint8_t head[meshtastic::HEADER_LEN];
    meshtastic::packHeader(h, head);
    sim_lora_inject(std::string((const char*)head, sizeof(head)) + body);
    run(30);
}
static void announce(uint32_t node, const char* longName, const char* shortName, double lat, double lon, int hops) {
    meshtastic::User u;
    u.id = meshtastic::nodeIdString(node);
    u.longName = longName;
    u.shortName = shortName;
    fromNode(node, meshtastic::PORT_NODEINFO, meshtastic::encodeUser(u), hops);
    meshtastic::Position p;
    p.hasLatLon = true;
    p.lat = lat;
    p.lon = lon;
    fromNode(node, meshtastic::PORT_POSITION, meshtastic::encodePosition(p), hops);
}

static void logQso(const char* call, const char* freq, const char* mode, const char* note) {
    key('n');
    type(call); enter();
    del(10); type(freq); enter();
    del(8); type(mode); enter();
    enter(); enter();
    type(note); enter();
}

static std::string readSd(const char* path) {
    std::string out;
    storage::readFile(path, out);
    return out;
}

int main() {
    srand(7);
    setup();
    run();

    // The node announces itself once the radio is up.
    EXPECT(!sim_lora_sent.empty());
    if (!sim_lora_sent.empty()) {
        meshtastic::Header h;
        meshtastic::Data d;
        meshtastic::User u;
        EXPECT(meshtastic::decodeData(openPacket(sim_lora_sent[0], h), d) && d.portnum == meshtastic::PORT_NODEINFO);
        EXPECT(meshtastic::decodeUser(d.payload, u) && u.longName == "TA3BLD" && u.shortName == "3BLD" && u.id == "!12345678");
        EXPECT(h.from == 0x12345678 && h.to == meshtastic::BROADCAST && h.channel == 0x08);
    }

    announce(0xa1b2c3d4, "TA3XYZ Karsiyaka", "XYZ", 38.46, 27.11, 0);
    announce(0x0badcafe, "Bornova Repeater", "BRN", 38.47, 27.22, 1);
    announce(0x5eed1234, "TA4ABC mobil", "4ABC", 38.08, 27.02, 2);
    fromNode(0xa1b2c3d4, meshtastic::PORT_TEXT, "Merhaba, kanalda kimse var mi?");
    fromNode(0x5eed1234, meshtastic::PORT_TEXT, "G\xc3\xbcnayd\xc4\xb1n! Sel\xc3\xa7uk yolunday\xc4\xb1m, sinyal nas\xc4\xb1l?", 2);
    fromNode(0xa1b2c3d4, meshtastic::PORT_TEXT, "Seni 2 atlamayla aliyorum", 0);
    EXPECT(mesh::messages().size() == 3 && mesh::unread() == 3 && mesh::nodes().size() == 3);
    sim_lora_inject(std::string(40, '\x55'));          // other channel / garbage must not crash or show up
    run(30);
    sim_lora_inject("short");
    run();
    EXPECT(mesh::messages().size() == 3 && mesh::stats().otherChannel == 1);
    shot("launcher");

    enter();                                   // Logbook
    logQso("dl1abc", "14.205", "ssb", "");
    logQso("g4xyz", "14.074", "ft8", "");
    logQso("ta1kk", "145.500", "fm", "Istanbul");
    logQso("ja1abc", "21.025", "cw", "");
    logQso("w1aw", "7.030", "cw", "");
    EXPECT(logbook::total() == 5);
    key('.'); key('.');                        // select TA1KK (third newest)
    key('d');
    shot("logbook-delete");
    key('y');
    {
        std::string file = readSd("/HamOS/log.adi");
        EXPECT(logbook::total() == 4 && file.find("TA1KK") == std::string::npos);
        EXPECT(file.find("<EOH>") != std::string::npos && file.find("DL1ABC") != std::string::npos &&
               file.find("W1AW") != std::string::npos && adif::parse(file).size() == 4);
        EXPECT(file.find("<RST_SENT:3>599") != std::string::npos);
    }
    key(';'); key(';');
    shot("logbook");
    key('n'); type("vk2abc");
    shot("logbook-new-country");
    back();
    key('n'); type("w1aw"); enter(); enter(); enter(); enter(); enter(); type("again");
    shot("logbook-dupe");
    back(); back();

    openNext();                                // Mesh
    shot("mesh-chat");
    EXPECT(mesh::unread() == 0);
    size_t sentBefore = sim_lora_sent.size();
    type("Selam, TA3BLD Izmir'den. 73!"); enter();
    run(3000);
    EXPECT(sim_lora_sent.size() == sentBefore + 1);
    if (sim_lora_sent.size() > sentBefore) {
        meshtastic::Header h;
        meshtastic::Data d;
        EXPECT(meshtastic::decodeData(openPacket(sim_lora_sent.back(), h), d) && d.portnum == meshtastic::PORT_TEXT &&
               d.payload == "Selam, TA3BLD Izmir'den. 73!");
        sim_lora_inject(sim_lora_sent.back());     // a neighbour repeats it
        run();
        EXPECT(mesh::messages().back().mine && mesh::messages().back().sent && mesh::messages().back().relayed);
    }
    fromNode(0x0badcafe, meshtastic::PORT_TEXT, "73 TA3BLD, 5/9 Bornova", 1);
    shot("mesh-chat-sent");
    tab();
    shot("mesh-nodes");
    tab();
    shot("mesh-info");
    back();

    sim_telnet.inject("login: \r\nHello TA3BLD\r\n"
                      "DX de EA5XX:     14025.0  JA1ABC       CW 599 up 1                    1104Z\r\n"
                      "DX de DL2ABC:    21074.0  PY2XYZ       FT8 -12dB                      1105Z\r\n"
                      "DX de IK4XX:      7155.0  TA2AB        59 in Italy                    1106Z\r\n"
                      "DX de G3ABC:     28495.0  VK2DX        long path                      1107Z\r\n"
                      "DX de OH2XX:     14195.0  W1AW         special event                  1108Z\r\n"
                      "DX de SP5XX:     18130.0  ZS6ABC       cq cq                          1109Z\r\n");
    openNext();                                // DX Cluster
    shot("dx-cluster");
    back();

    sim_http_body =
        "<solar><solardata><updated> 13 Nov 2026 1105 GMT</updated><solarflux>148</solarflux><aindex>8</aindex>"
        "<kindex>2</kindex><xray>B7.4</xray><sunspots>112</sunspots><calculatedconditions>"
        "<band name=\"80m-40m\" time=\"day\">Fair</band><band name=\"30m-20m\" time=\"day\">Good</band>"
        "<band name=\"17m-15m\" time=\"day\">Good</band><band name=\"12m-10m\" time=\"day\">Fair</band>"
        "<band name=\"80m-40m\" time=\"night\">Good</band><band name=\"30m-20m\" time=\"night\">Good</band>"
        "<band name=\"17m-15m\" time=\"night\">Fair</band><band name=\"12m-10m\" time=\"night\">Poor</band>"
        "</calculatedconditions></solardata></solar>";
    openNext();                                // Solar
    shot("solar");
    back();

    openNext();                                // GPS
    shot("gps");
    back();

    openNext();                                // Locator
    type("FN31pr");
    shot("locator");
    back();

    openNext();                                // Calculator
    key('.'); enter();
    shot("calculator-swr");
    back(); back();

    openNext();                                // Morse
    type("cq de ta3bld");
    shot("morse-send");
    tab(); run(3000);
    shot("morse-train");
    back();

    openNext();                                // UTC Clock
    shot("clock");
    back();

    openNext();                                // Reference
    shot("reference");
    back();

    openNext();                                // Settings
    for (int i = 0; i < 9; i++) key('.');
    shot("settings");

    if (failures) fprintf(stderr, "%d simulator checks FAILED\n", failures);
    else fprintf(stderr, "all simulator checks passed\n");
    return failures ? 1 : 0;
}
