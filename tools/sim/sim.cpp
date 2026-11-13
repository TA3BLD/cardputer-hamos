// Runs the real HamOS code on a PC with simulated hardware: presses keys from
// a script, feeds fake GPS / LoRa / network data, records what would be drawn
// and checks a few end-to-end results. See run.sh.
#include "main.cpp"

#include <RadioLib.h>
#include <WiFiUdp.h>
#include "core/Log.h"
#include "core/LoraAprs.h"
#include "lib/Aprs.h"
#include "lib/Ft8.h"
#include "lib/Ft8Decoder.h"
#include "lib/Meshtastic.h"
#include "lib/Wsjtx.h"
#include "mbedtls/aes.h"

uint32_t sim_ms = 1000;
static long long sim_epoch_ms = (long long)time(nullptr) / 15 * 15000;     // a period boundary
long long sim_utc_ms() { return sim_epoch_ms + sim_ms; }
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

static const char* kRigHost = "192.168.1.50";
static const char* kAprsHost = "rotate.aprs2.net";
static const char* kClusterHost = "dxc.ve7cc.net";

static void run(int ms = 60) {
    static uint32_t lastGps = 0, lastRig = 0, lastKeepAlive = 0;
    for (int t = 0; t < ms; t += 10) {
        sim_ms += 10;
        if (sim_ms - lastGps >= 1000) { lastGps = sim_ms; feedGps(); }
        if (sim_ms - lastRig >= 3000) { lastRig = sim_ms; sim_net[kRigHost].inject("14074000\nUSB\n2400\n"); }   // rigctld answers
        if (sim_ms - lastKeepAlive >= 20000) { lastKeepAlive = sim_ms; sim_net[kAprsHost].inject("# aprsc 2.1.15 keepalive\r\n"); }
        loop();
    }
}

// ---- microphone: queue audio and let the app consume all of it ----
// ---- FT8 over the air: 15 s periods ----
static void runUntilPeriodEnd() {          // stop 20 ms before the next period starts
    while (sim_utc_ms() % 15000 < 14980) run(10);
}
static std::vector<int16_t> ft8Period(const std::vector<std::pair<float, std::string>>& signals, double snrDb) {
    std::vector<double> mix(15 * 8000, 0.0);
    double noiseSigma = 900;
    double amp = std::sqrt(2 * noiseSigma * noiseSigma * 2500.0 / 4000.0 * std::pow(10.0, snrDb / 10));
    for (const auto& sig : signals) {
        uint8_t p[10], t[ft8::kSymbols];
        ft8::pack(sig.second, p);
        ft8::tones(p, t);
        ft8::Synth synth(t, sig.first, 8000, (int)amp, 0.5f);
        int16_t buf[512];
        size_t pos = 0;
        for (size_t n; (n = synth.read(buf, 512)) > 0;)
            for (size_t i = 0; i < n && pos < mix.size(); i++) mix[pos++] += buf[i];
    }
    std::vector<int16_t> out;
    for (double v : mix) {
        double g = 0;                                   // roughly Gaussian noise
        for (int i = 0; i < 12; i++) g += (double)rand() / RAND_MAX;
        out.push_back((int16_t)std::max(-32767.0, std::min(32767.0, v + (g - 6) * noiseSigma)));
    }
    return out;
}
static void hearPeriod(const std::vector<std::pair<float, std::string>>& signals, double snrDb = -8) {
    runUntilPeriodEnd();
    std::vector<int16_t> audio = ft8Period(signals, snrDb);
    sim_mic.insert(sim_mic.end(), audio.begin(), audio.end());
    run(15000);
}
// What HamOS sent through the speaker during the last period, decoded.
static std::vector<std::string> decodeSpeaker() {
    std::vector<int16_t> audio = sim_speaker;
    audio.resize(15 * 8000, 0);
    sim_speaker.clear();
    ft8::Decoder d;
    d.begin(2);
    ft8::Resampler r;
    for (size_t i = 0; i < audio.size(); i += 256) {
        int16_t out[256];
        d.feed(out, r.process(audio.data() + i, 256, out));
    }
    std::vector<ft8::Decoded> found;
    d.startDecode();
    while (d.step(found)) {}
    std::vector<std::string> texts;
    for (const auto& f : found) texts.push_back(f.text);
    return texts;
}
static bool contains(const std::vector<std::string>& v, const std::string& prefix) {
    for (const std::string& s : v)
        if (s.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

static void hear(const std::vector<int16_t>& audio, double gain = 0.3, int noise = 400) {
    for (int16_t v : audio) sim_mic.push_back((int16_t)(v * gain + rand() % (2 * noise) - noise));
    while (!sim_mic.empty()) run(10);
    run(60);
}

// ---- keyboard: every press is followed by a release ----
typedef Keyboard_Class::KeysState Keys;
static void press(const Keys& s) {
    M5Cardputer.Keyboard.script.push_back(s);
    M5Cardputer.Keyboard.script.push_back(Keys());
    run();
}
static void key(char c) { Keys s; s.word.push_back(c); press(s); }
static void type(const char* text) { for (const char* p = text; *p; p++) key(*p); }
static void enter() { Keys s; s.enter = true; press(s); }
static void tab() { Keys s; s.tab = true; press(s); }
static void del(int n = 1) { while (n--) { Keys s; s.backspace = true; press(s); } }
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

    // Keyboard handling, in the launcher: Fn+arrow keys move the selection, and
    // fast typing where keys overlap must not repeat characters.
    {
        Keys right; right.fn = true; right.right = true;
        Keys left; left.fn = true; left.left = true;
        press(right); press(right);
        enter();                                    // third tile
        EXPECT(std::string(current->name()) == "APRS");
        Keys esc; esc.fn = true; esc.esc = true;    // Fn + ` = ESC: back to the launcher
        press(esc);
        EXPECT(current == &launcher);
        press(left); press(left);
        key('/'); enter();                          // plain / works as "right" too: Mesh, chat input active
        EXPECT(std::string(current->name()) == "Mesh");
        Keys a, ab, b, shiftOnly, shiftH;
        a.word = {'a'}; ab.word = {'a', 'b'}; b.word = {'b'};
        shiftOnly.shift = true; shiftH.shift = true; shiftH.word = {'H'};
        Keys h; h.word = {'h'};
        for (const Keys& step : {a, ab, b, Keys(), shiftOnly, shiftH, h, Keys()}) M5Cardputer.Keyboard.script.push_back(step);
        run(200);
        Keys semicolon; semicolon.word = {';'};     // without Fn, ; is just text here
        press(semicolon);
        EXPECT(meshApp.inputForTest() == "abH;");
        del(4);
        EXPECT(meshApp.inputForTest().empty());
        back();
        key(',');                                   // launcher selection back on the first tile
    }

    // Line wrapping used by the chat and decoder screens.
    {
        auto w = ui::wrapLines("one two three four", 9);
        EXPECT(w.size() == 3 && w[0] == "one two " && w[1] == "three " && w[2] == "four");
        w = ui::wrapLines("abcdefghijkl\nxy", 5);
        EXPECT(w.size() == 4 && w[0] == "abcde" && w[1] == "fghij" && w[2] == "kl" && w[3] == "xy");
        EXPECT(ui::wrapLines("", 10).empty() && ui::ascii("G\xc3\xbcnayd\xc4\xb1n \xf0\x9f\x98\x80") == "Gunaydin ?");
        // Turkish letters count as one letter each, and survive; emoji do not.
        w = ui::wrapLines("\xc3\xa7\xc4\x9f\xc4\xb1\xc3\xb6\xc5\x9f\xc3\xbc \xc3\x87\xc4\x9e\xc4\xb0\xc3\x96\xc5\x9e\xc3\x9c", 7);
        EXPECT(w.size() == 2 && w[0] == "\xc3\xa7\xc4\x9f\xc4\xb1\xc3\xb6\xc5\x9f\xc3\xbc " && ui::glyphCount(w[1]) == 6);
        EXPECT(ui::displayable("G\xc3\xbcnayd\xc4\xb1n \xf0\x9f\x98\x80\t!\r\nx") == "G\xc3\xbcnayd\xc4\xb1n ? !\nx");
        EXPECT(ui::wrapLines("a\n\nb", 5).size() == 3);
    }

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
    // ---- APRS-IS: login, nearby stations and a message for us ----
    {
        const std::string& login = sim_net_sent[kAprsHost];
        EXPECT(login.find("user TA3BLD-7 pass " + std::to_string(aprs::passcode("TA3BLD")) + " vers HamOS") == 0);
        EXPECT(login.find("filter r/38.42/27.14/50") != std::string::npos);
    }
    sim_net[kAprsHost].inject("# aprsc 2.1.15-gc67551b\r\n# logresp TA3BLD-7 verified, server T2TURKEY\r\n"
                              "TA3XYZ-9>APDR16,TCPIP*,qAC,T2TURKEY:=3827.60N/02706.60E>Karsiyaka sahil, 145.500\r\n"
                              "TA3ABC-1>APRS,TCPIP*,qAC,T2TURKEY:!3828.20N/02713.20E#Bornova digi W1\r\n"
                              "YM3KDS>APRS,qAR,TA3ABC-1:!3813.70N/02650.10E-Seferihisar\r\n"
                              "TA4KA-7>APZHAM,WIDE1-1,qAR,TA3ABC-1:/131109z3805.00N/02701.00E[yuruyus\r\n"
                              "TA3XYZ-9>APDR16,TCPIP*,qAC,T2TURKEY::TA3BLD-7 :Selam, cikista misin?{7\r\n"
                              "TA2ZZ>APRS,TCPIP*::TA9XXX   :not for us{1\r\n");
    run(200);
    EXPECT(aprsis::online() && aprsis::stations().size() == 4 && aprsis::messages().size() == 1 && aprsis::unread() == 1);
    EXPECT(sim_net_sent[kAprsHost].find("TA3BLD-7>APZHAM,TCPIP*::TA3XYZ-9 :ack7\r\n") != std::string::npos);
    sim_net[kAprsHost].inject("TA3XYZ-9>APDR16,TCPIP*,qAC,T2TURKEY::TA3BLD-7 :Selam, cikista misin?{7\r\n");   // repeat
    run(100);
    EXPECT(aprsis::messages().size() == 1);
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
    // Contest mode: serial numbers instead of a note.
    config::set("contest", "1");
    key('n'); type("ea5xx"); enter();
    del(10); type("14.250"); enter();
    del(8); type("ssb"); enter();
    enter(); enter();
    type("047");
    shot("logbook-contest");
    enter();
    EXPECT(logbook::total() == 5 && readSd("/HamOS/log.adi").find("<STX:3>001 <SRX:3>047 <MY_SIG:4>POTA") != std::string::npos);
    config::erase("contest");
    shot("logbook");
    key('n'); type("vk2abc");
    shot("logbook-new-country");
    back();
    key('n'); type("w1aw"); enter();
    del(10); type("7.030"); enter();           // the form starts from the radio's frequency (14.074 SSB)
    del(8); type("cw"); enter();
    enter(); enter(); type("again");
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

    // ---- APRS pages, then the audio modem ----

    openNext();                                // APRS (third tile)
    shot("aprs-stations");
    tab();
    type("ta3xyz-9 Evet, Kordon'dayim. 73"); enter();
    EXPECT(sim_net_sent[kAprsHost].find("TA3BLD-7>APZHAM,TCPIP*::TA3XYZ-9 :Evet, Kordon'dayim. 73{1\r\n") != std::string::npos);
    sim_net[kAprsHost].inject("TA3XYZ-9>APDR16,TCPIP*::TA3BLD-7 :ack1\r\n");
    run(100);
    EXPECT(aprsis::messages().size() == 2 && aprsis::messages().back().acked && aprsis::unread() == 0);
    shot("aprs-messages");
    tab();                                     // radio modem page: microphone on
    {
        std::string frame;
        ax25::encodeFrame("TA3KM-9>APRS,WIDE1-1,WIDE2-1:!3824.10N/02709.30E>Konak, RF'ten duyuldu", frame);
        hear(ax25::modulate(frame));
        ax25::encodeFrame("YM3IZM>APRS:!3827.00N/02710.00E#Izmir digi", frame);
        hear(ax25::modulate(frame), 0.15, 300);
        EXPECT(aprsis::stations().size() == 6);
        bool radio = false;
        for (const auto& st : aprsis::stations()) radio |= st.call == "TA3KM-9" && st.source == aprsis::AUDIO;
        EXPECT(radio);
    }
    shot("aprs-modem");

    // ---- LoRa-APRS on 433.775 MHz: the radio switches over from Meshtastic ----
    {
        config::set("lora_mode", "aprs");
        config::set("lora_igate", "1");
        mesh::begin();
        loraaprs::begin();
        EXPECT(loraaprs::ready() && !mesh::ready());
        size_t before = sim_lora_sent.size();
        std::string sentBefore = sim_net_sent[kAprsHost];
        sim_lora_inject(std::string("<\xff\x01") + "TA3LOR-7>APLRT1,WIDE1-1:!3826.00N/02712.00E>LoRa tracker");
        run(100);
        sim_lora_inject("TA3MIC-9>S32U6T,WIDE1-1:`(_fn\"Oj/]Mic-E radio");                 // no header, Mic-E
        run(100);
        bool lora = false, mice = false;
        for (const auto& st : aprsis::stations()) {
            lora |= st.call == "TA3LOR-7" && st.source == aprsis::LORA;
            mice |= st.call == "TA3MIC-9" && std::fabs(st.lat - 33.42733) < 1e-3;
        }
        EXPECT(lora && mice);
        // receive-only iGate: forwarded to APRS-IS with qAR and our call
        EXPECT(sim_net_sent[kAprsHost].substr(sentBefore.size()).find(
                   "TA3LOR-7>APLRT1,WIDE1-1,qAR,TA3BLD-7:!3826.00N/02712.00E>LoRa tracker\r\n") != std::string::npos);
        tab();                                 // modem page -> station page
        key('b');
        run(2000);
        EXPECT(sim_lora_sent.size() == before + 1 &&
               sim_lora_sent.back() == std::string("<\xff\x01") + "TA3BLD-7>APZHAM,WIDE1-1:!3825.42N/02708.57E[HamOS");
        // A message for us over LoRa is acknowledged over LoRa.
        sim_lora_inject(std::string("<\xff\x01") + "TA3LOR-7>APLRT1,WIDE1-1::TA3BLD-7 :Duyuyor musun?{42");
        run(3000);
        EXPECT(sim_lora_sent.back() == std::string("<\xff\x01") + "TA3BLD-7>APZHAM,WIDE1-1::TA3LOR-7 :ack42");
        shot("aprs-lora");
        config::set("lora_mode", "mesh");
        config::erase("lora_igate");
        loraaprs::begin();
        mesh::begin();
        EXPECT(mesh::ready() && !loraaprs::ready());
        tab(); tab();                          // on to the modem page again
    }
    sim_speaker.clear();
    key('b');                                  // beacon through the speaker
    run(200);
    {
        ax25::Demodulator check;               // what came out of the speaker must be a valid packet
        check.feed(sim_speaker.data(), sim_speaker.size());
        EXPECT(check.packets.size() == 1 && check.packets[0] == "TA3BLD-7>APZHAM,WIDE1-1,WIDE2-1:!3825.42N/02708.57E[HamOS");
    }
    back();

    // ---- Map: own position, APRS stations and mesh nodes, three levels of detail ----
    openNext();
    EXPECT(std::string(current->name()) == "Map");
    shot("map");
    key('g');                                  // Maidenhead grid on
    key('-'); key('-'); key('-');              // 1200 km
    shot("map-region");
    key('g');
    key('-'); key('-'); key('-');              // 10000 km
    shot("map-world");
    for (int i = 0; i < 6; i++) key('=');      // back to 150 km
    tab(); tab();
    shot("map-selected");
    back();

    // ---- FT8: decode three stations, answer the CQ from DX1ABC, complete and log the contact ----
    openNext();                                // FT8 (fourth tile)
    {
        sim_speaker.clear();
        // Period A: three stations; DX1ABC (a country not in the log) calls CQ.
        hearPeriod({{1210.0f, "CQ DX1ABC PK04"}, {812.5f, "TA1ABC DL1XYZ -05"}, {2015.0f, "CQ JA1ABC PM95"}}, -8);
        run(400);
        EXPECT(ft8App.selectForTest("CQ DX1ABC"));
        run(20);
        printf("S ft8-decodes\n");
        enter();                               // answer at once: this period is the other half of the minute
        runUntilPeriodEnd();
        std::vector<std::string> sent = decodeSpeaker();
        EXPECT(sent.size() == 1 && sent[0] == "DX1ABC TA3BLD KM38");
        // Period C: DX1ABC sends a report; HamOS answers with R and its report.
        hearPeriod({{1210.0f, "TA3BLD DX1ABC -10"}, {2015.0f, "CQ JA1ABC PM95"}}, -9);
        runUntilPeriodEnd();
        run(15000);
        sent = decodeSpeaker();
        EXPECT(sent.size() == 1 && contains(sent, "DX1ABC TA3BLD R-"));
        printf("S ft8-qso\n");                // (no shot(): it would move the clock into the next period)
        // Period E: RR73 from DX1ABC; HamOS sends 73 and logs the contact.
        hearPeriod({{1210.0f, "TA3BLD DX1ABC RR73"}}, -9);
        runUntilPeriodEnd();
        run(15000);
        sent = decodeSpeaker();
        EXPECT(sent.size() == 1 && sent[0] == "DX1ABC TA3BLD 73");
        std::string log = readSd("/HamOS/log.adi");
        EXPECT(log.find("<CALL:6>DX1ABC") != std::string::npos && log.find("<MODE:3>FT8") != std::string::npos &&
               log.find("<RST_RCVD:3>-10") != std::string::npos && log.find("<GRIDSQUARE:4>PK04") != std::string::npos);
        // Nothing more is sent once the contact is complete.
        hearPeriod({}, -9);
        runUntilPeriodEnd();
        run(15000);
        EXPECT(decodeSpeaker().empty());
    }
    back();

    sim_net[kClusterHost].inject("login: \r\nHello TA3BLD\r\n"
                                 "DX de EA5XX:     14025.0  JA1ABC       CW 599 up 1                    1104Z\r\n"
                                 "DX de DL2ABC:    21074.0  PY2XYZ       FT8 -12dB                      1105Z\r\n"
                                 "DX de IK4XX:      7155.0  TA2AB        59 in Italy                    1106Z\r\n"
                                 "DX de G3ABC:     28495.0  VK2DX        long path                      1107Z\r\n"
                                 "DX de OH2XX:     14195.0  W1AW         special event                  1108Z\r\n"
                                 "DX de SP5XX:     18130.0  ZS6ABC       cq cq                          1109Z\r\n");
    openNext();                                // DX Cluster
    shot("dx-cluster");
    EXPECT(sim_net_sent[kClusterHost] == "TA3BLD\r\n");
    back();

    // ---- FT8: what WSJT-X on the PC decodes, and a contact it logs ----
    {
        auto decode = [](int snr, uint32_t df, const char* text) {
            wsjtx::Writer w(wsjtx::DECODE);
            w.u8(1); w.u32(43200000); w.u32((uint32_t)snr); w.f64(0.2); w.u32(df); w.utf8("~"); w.utf8(text); w.u8(0); w.u8(0);
            sim_udp.push_back(w.out);
        };
        wsjtx::Writer st(wsjtx::STATUS);
        st.u64(14074000); st.utf8("FT8"); st.utf8("LU5ABC"); st.utf8("-12"); st.utf8("FT8"); st.u8(1); st.u8(0); st.u8(1);
        st.u32(1500); st.u32(1500); st.utf8("TA3BLD"); st.utf8("KM38");
        sim_udp.push_back(st.out);
        decode(-3, 812, "CQ TA1ABC KN41");
        decode(-17, 1034, "DL1ABC SP5XYZ JO91");
        decode(-12, 1290, "CQ DX VK2DX QF56");
        decode(2, 1523, "JA1ABC EA5XX RR73");
        decode(-9, 1650, "CQ POTA K4ABC EM73");
        decode(-14, 1871, "TA3BLD LU5ABC -12");
        decode(-20, 2210, "CQ ZS6ABC KG33");
        sim_udp.push_back(std::string("not a wsjt-x packet"));
        wsjtx::Writer lg(wsjtx::LOGGED_ADIF);
        lg.utf8("<adif_ver:5>3.1.0<programid:6>WSJT-X<EOH><call:6>LU5ABC <gridsquare:4>GF05 <mode:3>FT8 <rst_sent:3>-12 "
                "<rst_rcvd:3>-09 <qso_date:8>20261113 <time_on:6>115800 <band:3>20m <freq:9>14.075500 <EOR>");
        sim_udp.push_back(lg.out);
    }
    openNext();                                // WSJT-X companion
    run(300);
    EXPECT(wsjtxlink::decodes().size() == 7 && wsjtxlink::logged() == 1 && logbook::total() == 7);
    EXPECT(readSd("/HamOS/log.adi").find("<CALL:6>LU5ABC <QSO_DATE:8>20261113 <TIME_ON:4>1158 <FREQ:9>14.075500 <BAND:3>20m <MODE:3>FT8") != std::string::npos);
    shot("wsjtx");
    back();

    sim_http["hamqsl.com"] =
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

    // ---- Satellites: download the orbit data, then predict passes ----
    sim_http["celestrak.org"] =
        "ISS (ZARYA)             \r\n"
        "1 25544U          26300.50000000  .00000000  00000-0  30000-3 0    05\r\n"
        "2 25544  51.6400 100.0000 0005000  90.0000 270.0000 15.50000000    03\r\n"
        "SO-50\r\n"
        "1 27607U          26310.25000000  .00000000  00000-0  50000-4 0    03\r\n"
        "2 27607  64.5500 200.0000 0065000 300.0000  60.0000 14.76000000    04\r\n"
        "AO-7\r\n"
        "1 07530U          26305.75000000  .00000000  00000-0  10000-3 0    00\r\n"
        "2 07530 101.9000 330.0000 0012000  20.0000 340.0000 12.54000000    08\r\n"
        "QO-100 (GEO, unsupported)\r\n"
        "1 43700U          26305.75000000  .00000000  00000-0  00000-0 0    00\r\n"
        "2 43700   0.0200 100.0000 0002000  20.0000 340.0000  1.00270000    08\r\n";
    openNext();                                // Satellites
    key('u');
    run(200);
    EXPECT(readSd("/HamOS/tle.txt").find("SO-50") != std::string::npos);
    shot("satellites");
    enter();
    shot("satellite-passes");
    back(); back();

    openNext();                                // GPS
    shot("gps");
    back();

    // ---- Decoder: Morse and RTTY from the microphone ----
    openNext();
    hear(cw::synthesize("CQ CQ CQ DE TA1ABC TA1ABC PSE K", 20), 0.3, 500);
    hear(cw::synthesize("TA1ABC DE TA3BLD 599 IZMIR 73", 26), 0.2, 500);    // a faster station answers
    shot("decoder-cw");
    tab();
    hear(rtty::modulate("RYRYRY CQ CQ DE TA1ABC TA1ABC PSE K\r\nUR 599 599 NR 001 73!"), 0.3, 500);
    shot("decoder-rtty");
    sim_speaker.clear();
    enter();                                   // send a CQ through the speaker
    run(200);
    {
        rtty::Demodulator check;
        check.feed(sim_speaker.data(), sim_speaker.size());
        EXPECT(check.text == "RYRYRY CQ CQ CQ DE TA3BLD TA3BLD TA3BLD K");
    }
    back();

    openNext();                                // Locator
    type("FN31pr");
    shot("locator");
    back();

    openNext();                                // Repeaters
    shot("repeaters");
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

    openNext();                                // Guide
    EXPECT(std::string(current->name()) == "Guide");
    shot("guide");
    key('.'); key('.'); enter();               // the band plan
    key('.'); key('.'); key('.');
    shot("guide-page");
    for (int i = 0; i < 30; i++) key('/');     // page down to the end, never past it
    back(); back();

    openNext();                                // Countries
    EXPECT(std::string(current->name()) == "Countries");
    key('t'); key('.'); key('.');
    shot("countries");
    key('j'); enter();                         // the first country starting with J
    shot("country");
    back(); back();

    openNext();                                // Settings
    for (int i = 0; i < 20; i++) key('.');
    shot("settings");

    if (failures) fprintf(stderr, "%d simulator checks FAILED\n", failures);
    else fprintf(stderr, "all simulator checks passed\n");
    return failures ? 1 : 0;
}
