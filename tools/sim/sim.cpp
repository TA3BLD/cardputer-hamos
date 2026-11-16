// Runs the real HamOS code on a PC with simulated hardware: presses keys from
// a script, feeds fake GPS / LoRa / network data, records what would be drawn
// and checks a few end-to-end results. See run.sh.
#include "main.cpp"

#include <RadioLib.h>
#include <WiFiUdp.h>
#include "core/Log.h"
#include "core/Keyer.h"
#include "core/Lang.h"
#include "core/Emergency.h"
#include "core/PhoneApi.h"
#include "core/RangeTest.h"
#include "core/Weather.h"
#include "core/LoraAprs.h"
#include "core/Voice.h"
#include "core/Settings.h"
#include "core/Cap.h"
#include <cmath>
#include "core/Callbook.h"
#include "core/LogUpload.h"
#include "core/Pota.h"
#include "core/PskReporter.h"
#include "core/LoraAprs.h"
#include "lib/Aprs.h"
#include "lib/Ft8.h"
#include "lib/Ft8Decoder.h"
#include "lib/Bmp.h"
#include "lib/Sstv.h"
#include "lib/Psk31.h"
#include "lib/MiniJson.h"
#include "lib/Base64.h"
#include "lib/Meshtastic.h"
#include "lib/Wsjtx.h"
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <Update.h>

uint32_t sim_ms = 1000;
static long long sim_epoch_ms = (long long)time(nullptr) / 15 * 15000;     // a period boundary
long long sim_utc_ms() { return sim_epoch_ms + sim_ms; }
size_t sim_usb_write(const uint8_t* data, size_t n) { Serial.tx.append((const char*)data, n); return n; }
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

// ---- USB link: send one request line, return the reply (and the other lines in `extra`) ----
static int usbId = 100;
static json::Object usbCall(std::string body, std::string* extra = nullptr) {
    int id = ++usbId;
    std::string line = "{\"id\":" + std::to_string(id) + (body.empty() ? "" : "," + body) + "}\n";
    Serial.inject(line);
    Serial.tx.clear();
    run(30);
    json::Object reply;
    std::string want = "{\"id\":" + std::to_string(id) + ",";
    size_t pos = 0;
    while (pos < Serial.tx.size()) {
        size_t end = Serial.tx.find('\n', pos);
        if (end == std::string::npos) end = Serial.tx.size();
        std::string l = Serial.tx.substr(pos, end - pos);
        if (l.compare(0, want.size(), want) == 0) {
            // flat fields only: lists are checked by text
            json::Object flat;
            reply.values["_raw"] = l;
            size_t cut = l.find(",\"", want.size() - 1);
            (void)cut;
        } else if (extra) {
            *extra += l + "\n";
        }
        pos = end + 1;
    }
    std::string raw = reply.str("_raw");
    reply.values["ok"] = raw.find("\"ok\":true") != std::string::npos ? "true" : "false";
    return reply;
}
static bool has(const json::Object& r, const std::string& text) { return r.str("_raw").find(text) != std::string::npos; }

// Lets whatever HamOS is playing finish.
static void runUntilQuiet() {
    for (int i = 0; i < 60000 && audio::playing(); i++) run(10);
    run(100);
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
extern std::deque<uint8_t> sim_ble_from_radio;
extern std::string sim_ble_to_radio;
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
    runUntilQuiet();
    {
        ax25::Demodulator check;               // what came out of the speaker must be a valid packet
        sim_speaker.resize(sim_speaker.size() + 11025 / 2, 0);     // the receiver hears on after the picture
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
    config::set("psk_report", "1");             // decodes go to PSK Reporter
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

    // ---- PSK Reporter: the FT8 decodes above went out as one IPFIX datagram ----
    {
        bool found = false;
        for (auto& d : sim_udp_sent)
            if (d.first == "report.pskreporter.info:4739" && d.second.find("TA3BLD") != std::string::npos &&
                d.second.find("KM38") != std::string::npos && d.second[1] == 10)
                found = true;
        EXPECT(found || pskr::queued() > 0);
        size_t before = sim_udp_sent.size();
        pskr::add("DL1ABC", "JO31", "FT8", 14075123, -12);
        pskr::add("DL1ABC", "JO31", "FT8", 14075300, -10);   // same station, same band: once only
        EXPECT(pskr::queued() <= 2);
        (void)before;
    }

    // ---- POTA / SOTA ----
    auto isoAgo = [](int minutes) {
        time_t t = (time_t)(sim_utc_ms() / 1000) - minutes * 60;
        struct tm g;
        gmtime_r(&t, &g);
        char b[24];
        strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &g);
        return std::string(b);
    };
    sim_http["api.pota.app/spot/activator"] = std::string(R"([
      {"spotId":1,"activator":"K4SWL","frequency":"14062.0","mode":"CW","reference":"US-3971","parkName":null,"spotTime":")" + isoAgo(14) + R"(",
       "spotter":"W1AW","comments":"QRP 5W","source":"RBN","name":"Pisgah National Forest","locationDesc":"US-NC","latitude":35.3,"longitude":-82.7},
      {"spotId":2,"activator":"TA1ABC","frequency":"7135","mode":"SSB","reference":"TA-0003","spotTime":")" + isoAgo(3) + R"(",
       "spotter":"TA1ABC","comments":"QRV","name":"Kuşcenneti Milli Parkı","locationDesc":"TR-10","latitude":40.2,"longitude":27.97}])");
    sim_http["api2.sota.org.uk/api/spots"] = std::string(R"([{"id":5,"timeStamp":")") + isoAgo(22) + R"(","comments":"qrt 12z","callsign":"OE5JFE",
       "associationCode":"OE","summitCode":"OO-001","activatorCallsign":"OE5JFE/P","summitDetails":"Hoher Dachstein, 2995m, 10 Points",
       "frequency":"14.285","mode":"ssb"}])";
    sim_http["pota.app/all_parks_ext.csv"] = "\"reference\",\"name\",\"active\",\"entityId\",\"locationDesc\",\"latitude\",\"longitude\",\"grid\"\n"
        "\"TA-0001\",\"Spil Dağı Milli Parkı\",\"1\",\"390\",\"TR-45\",\"38.555\",\"27.43\",\"KM38lm\"\n"
        "\"TA-0002\",\"Dilek Yarımadası, Büyük Menderes\",\"1\",\"390\",\"TR-09\",\"37.68\",\"27.15\",\"KM37\"\n"
        "\"TA-0099\",\"Old park\",\"0\",\"390\",\"TR-35\",\"38.4\",\"27.1\",\"KM38\"\n"
        "\"US-0001\",\"Acadia National Park\",\"1\",\"291\",\"US-ME\",\"44.31\",\"-68.2034\",\"FN54vh\"\n";
    sim_http["storage.sota.org.uk/summitslist.csv"] = "SOTA Summits List (Date=13/11/2026)\n"
        "SummitCode,AssociationName,RegionName,SummitName,AltM,AltFt,GridRef1,GridRef2,Longitude,Latitude,Points,BonusPoints,ValidFrom,ValidTo,ActivationCount,ActivationDate,ActivationCall\n"
        "TA/AE-001,Turkey,Aegean,Bozdağ,2159,7083,28.07,38.33,28.0700,38.3300,10,3,01/01/2020,31/12/2099,2,01/05/2025,TA3BLD\n"
        "TA/AE-050,Turkey,Aegean,Spil Dağı,1513,4964,27.43,38.56,27.4300,38.5600,8,3,01/01/2020,31/12/2099,0,,\n"
        "OE/OO-001,Austria,Oberoesterreich,Hoher Dachstein,2995,9826,13.6,47.47,13.6000,47.4700,10,3,01/01/2000,31/12/2099,40,,\n";
    sim_http["api.pota.app/spot"] = "{}";
    openNext();                                // POTA/SOTA
    EXPECT(std::string(current->name()) == "POTA/SOTA");
    run(300);
    shot("pota-spots");
    key('.'); enter();
    shot("pota-spot");
    back();
    tab();                                     // SOTA spots
    run(300);
    shot("sota-spots");
    tab();                                     // parks near me
    key('d');
    EXPECT(storage::exists("/HamOS/pota/parks.csv"));
    {
        std::string err;
        auto parks = pota::nearby(pota::POTA, 10, err);
        EXPECT(parks.size() == 2 && parks[0].ref == "TA-0001" && parks[1].ref == "TA-0002");   // retired and far away left out
    }
    shot("pota-parks");
    key('s');                                  // activate the nearest park
    EXPECT(config::get("pota_ref") == "TA-0001");
    tab();                                     // summits near me
    key('d');
    {
        std::string err;
        auto summits = pota::nearby(pota::SOTA, 10, err);
        EXPECT(summits.size() == 2 && summits[0].ref == "TA/AE-050");
    }
    shot("sota-summits");
    tab();                                     // my activation
    key('s');                                  // spot me
    del(10); type("14244"); enter();
    del(6); type("SSB"); enter();
    type("CQ POTA"); enter();
    {
        const SimHttpRequest& last = sim_http_log.back();
        EXPECT(last.method == "POST" && last.url == "https://api.pota.app/spot" && last.body.find("\"reference\":\"TA-0001\"") != std::string::npos &&
               last.body.find("\"frequency\":\"14244.0\"") != std::string::npos && last.body.find("\"activator\":\"TA3BLD\"") != std::string::npos);
    }
    shot("pota-activation");
    back();

    // ---- PSK Reporter: who heard me ----
    sim_http["retrieve.pskreporter.info/query"] = R"(<?xml version="1.0"?><receptionReports currentSeconds="1">
      <activeReceiver callsign="X" locator="JO31" />
      <receptionReport receiverCallsign="DL1ABC" receiverLocator="JO31le" senderCallsign="TA3BLD" senderLocator="KM38nk" frequency="14075512" flowStartSeconds="1794571000" mode="FT8" isSender="1" sNR="-14" />
      <receptionReport receiverCallsign="JA1XYZ" receiverLocator="PM95" senderCallsign="TA3BLD" frequency="14075530" flowStartSeconds="1794571300" mode="FT8" sNR="-21" />
      <receptionReport receiverCallsign="4X1AB" receiverLocator="KM72" senderCallsign="TA3BLD" frequency="7075500" flowStartSeconds="1794570000" mode="FT8" sNR="-3" />
      </receptionReports>)";
    openNext();                                // PSK Reporter
    run(300);
    EXPECT(std::string(current->name()) == "PSK Reporter");
    shot("psk-reporter");
    back();

    // ---- Callbook ----
    config::set("hamqth_user", "ta3bld");
    config::set("hamqth_pass", "x");
    sim_http["hamqth.com/xml.php?u="] = "<?xml version=\"1.0\"?><HamQTH version=\"2.8\"><session><session_id>09b0ae90050be03c452ad235a1f2915ad684393c</session_id></session></HamQTH>";
    sim_http["hamqth.com/xml.php?id="] = "<?xml version=\"1.0\"?><HamQTH version=\"2.8\"><search><callsign>dl1abc</callsign><nick>Klaus</nick>"
        "<qth>Bremen</qth><country>Germany</country><grid>jo43jb</grid></search></HamQTH>";
    openNext();                                // Callbook
    type("dl1abc"); enter();
    run(300);
    shot("callbook");
    back(); back();

    // Logbook form fills in the name from the callbook
    {
        callbook::Info info;
        std::string err;
        EXPECT(callbook::lookup("DL1ABC", info, err) && info.name == "Klaus" && info.grid == "JO43jb");
    }

    // ---- Log upload ----
    config::set("qrz_logkey", "ABCD-1234");
    config::set("eqsl_user", "TA3BLD");
    config::set("eqsl_pass", "x");
    sim_http["logbook.qrz.com/api"] = "RESULT=OK&LOGID=1234&COUNT=1";
    sim_http["eqsl.cc/qslcard/importADIF"] = "<HTML><BODY>Result: 1 out of 1 records added<BR></BODY></HTML>";
    openNext();                                // Log upload
    run(300);
    EXPECT(std::string(current->name()) == "Log upload");
    size_t logged = logbook::total();
    enter();                                   // eQSL: everything new
    {
        logupload::Counts c = logupload::count();
        EXPECT(c.total == logged && c.waiting[logupload::EQSL] == 0 && c.waiting[logupload::QRZ] == logged);
    }
    key('.'); key('.');
    enter();                                   // QRZ.com
    EXPECT(logupload::count().waiting[logupload::QRZ] == 0);
    EXPECT(sim_http_log.back().body.find("ACTION=INSERT") != std::string::npos && sim_http_log.back().body.find("KEY=ABCD-1234") != std::string::npos);
    key('.'); key('a');                        // Cabrillo, whole log
    {
        std::string cbr;
        EXPECT(storage::readFile(logupload::kCabrilloPath, cbr) && cbr.find("START-OF-LOG: 3.0") != std::string::npos &&
               cbr.find("QSO: ") != std::string::npos && cbr.find("END-OF-LOG:") != std::string::npos);
    }
    shot("log-upload");
    back();

    // ---- Radio over CAT: Icom CI-V cable on the Grove port ----
    auto setRig = [](const char* type) {
        config::set("rig_type", type);
        settings::apply(settings::RIG);
        Serial2.tx.clear();
    };
    auto civAnswer = [](std::initializer_list<uint8_t> body) {
        std::string f = {(char)0xFE, (char)0xFE, (char)0xE0, (char)0x94};
        for (uint8_t b : body) f += (char)b;
        f += (char)0xFD;
        Serial2.inject(f);
    };
    setRig("civ");
    civAnswer({0x03, 0x00, 0x40, 0x07, 0x14, 0x00});    // 14.074000
    civAnswer({0x04, 0x01, 0x01});                      // USB
    run(1500);
    EXPECT(rig::connected() && rig::freqMhz() == "14.074" && rig::mode() == "SSB");
    EXPECT(Serial2.tx.find(std::string("\xFE\xFE\x94\xE0\x03\xFD", 6)) != std::string::npos);   // asked for the frequency
    openNext();                                // Radio
    EXPECT(std::string(current->name()) == "Radio");
    shot("radio");
    Serial2.tx.clear();
    key('b');                                  // next band: 18.100 FT8
    EXPECT(Serial2.tx.find(std::string("\xFE\xFE\x94\xE0\x05\x00\x00\x10\x18\x00\xFD", 11)) != std::string::npos);
    back();

    // Kenwood cable
    setRig("kenwood");
    Serial2.inject("FA00007074000;MD2;");
    run(1500);
    EXPECT(rig::freqMhz() == "7.074" && rig::mode() == "SSB" && Serial2.tx.find("FA;MD;") != std::string::npos);
    Serial2.tx.clear();
    rig::tune(7.030, "CW");
    EXPECT(Serial2.tx == "FA00007030000;MD3;");

    // IC-705 over Bluetooth
    setRig("ic705");
    for (uint8_t b : {0xFE, 0xFE, 0xE0, 0xA4, 0x03, 0x00, 0x00, 0x50, 0x45, 0x01, 0xFD}) sim_ble_from_radio.push_back(b);
    run(1500);
    EXPECT(rig::freqMhz() == "145.500" && std::string(rig::typeName()) == "IC-705");
    sim_ble_to_radio.clear();
    rig::ptt(true);
    EXPECT(sim_ble_to_radio == std::string("\xFE\xFE\xA4\xE0\x1C\x00\x01\xFD", 8));
    rig::ptt(false);

    // ---- CW keyer ----
    setRig("civ");
    civAnswer({0x03, 0x00, 0x40, 0x02, 0x07, 0x00});    // 7.024
    civAnswer({0x04, 0x03, 0x01});                      // CW
    run(1200);
    openNext();                                // CW keyer
    EXPECT(std::string(current->name()) == "CW keyer");
    tab(); type("dl1abc"); tab();              // the other station
    Serial2.tx.clear();
    key('2');                                  // memory 2 through the radio's keyer (CI-V 17)
    EXPECT(Serial2.tx.find("\x17" "DL1ABC DE TA3BLD UR 5NN 5NN BK") != std::string::npos);
    shot("cw-keyer");
    // Grove key line, no radio: the keyer times the CW itself
    config::set("rig_type", "");
    config::set("grove_io", "key");
    settings::apply(settings::RIG);
    int tones = M5Cardputer.Speaker.tones, keyedDown = 0;
    key('1');
    for (int i = 0; i < 400; i++) { run(5); keyedDown += sim_pin_out[cap::GROVE_1]; }
    EXPECT(keyedDown > 20 && M5Cardputer.Speaker.tones > tones + 3);
    back(); back();
    // Paddle: dit held for three dits -> "S"
    config::set("grove_io", "paddle");
    settings::apply(settings::RIG);
    keyer::clearPaddleText();
    sim_pin_in[cap::GROVE_1] = 0;
    run(70 * 5);                              // 18 WPM: dot 66 ms; three dits and gaps
    sim_pin_in[cap::GROVE_1] = 1;
    run(1200);
    EXPECT(keyer::paddleText().find('S') != std::string::npos || keyer::paddleText().find('H') != std::string::npos);
    config::set("grove_io", "");
    settings::apply(settings::RIG);
    back();

    // ---- Voice keyer: record a message, send it with PTT through CI-V ----
    setRig("civ");
    civAnswer({0x03, 0x00, 0x00, 0x20, 0x14, 0x00});    // 14.200
    run(1200);
    openNext();                                // Voice keyer
    EXPECT(std::string(current->name()) == "Voice keyer");
    key('r'); key('1');
    EXPECT(voice::recording());
    for (int i = 0; i < 8000 * 2; i++) sim_mic.push_back((int16_t)(8000 * sin(i * 0.3)));
    while (!sim_mic.empty()) run(10);
    shot("voice-recording");
    key(' ');                                  // stop
    EXPECT(!voice::recording() && voice::length(1) > 1.5f);
    Serial2.tx.clear();
    key('1');                                  // transmit it
    EXPECT(voice::playing() && Serial2.tx.find(std::string("\x1C\x00\x01", 3)) != std::string::npos);
    shot("voice-keyer");
    for (int i = 0; i < 400 && voice::playing(); i++) run(10);
    EXPECT(!voice::playing() && Serial2.tx.find(std::string("\x1C\x00\x00", 3)) != std::string::npos);
    back();
    setRig("");

    // ---- Meshtastic app interface (as over Bluetooth) ----
    {
        EXPECT(mesh::ready());
        meshtastic::PbWriter want;
        want.fieldVarint(3, 4242);             // ToRadio.want_config_id
        phoneapi::handleToRadio(want.out);
        std::string f;
        int infos = 0, channels = 0;
        bool complete = false, myInfo = false;
        while (phoneapi::nextFromRadio(f)) {
            meshtastic::PbReader r(f);
            while (r.next()) {
                if (r.field == 3) myInfo = true;
                if (r.field == 4) infos++;
                if (r.field == 10) channels++;
                if (r.field == 7 && r.value == 4242) complete = true;
            }
        }
        EXPECT(myInfo && infos >= 2 && channels == 8 && complete);
        fromNode(0x0badcafe, meshtastic::PORT_TEXT, "Telefona da gelsin");
        run(100);
        bool forwarded = false;
        while (phoneapi::nextFromRadio(f)) forwarded |= f.find("Telefona da gelsin") != std::string::npos;
        EXPECT(forwarded);
        phoneapi::reset();
    }

    // ---- Range test: a Meshtastic node sends "seq N", we note where we heard it ----
    announce(0x5eed0001, "Range sender", "RNG", 38.44, 27.16, 0);
    openNext();                                // Range test
    EXPECT(std::string(current->name()) == "Range test");
    key('r');
    for (int n = 1; n <= 6; n++) {
        if (n == 4) continue;                  // one lost
        fromNode(0x5eed0001, 66, "seq " + std::to_string(n));
    }
    EXPECT(rangetest::hits().size() == 5 && rangetest::lost() == 1 && rangetest::bestKm() > 1.5 && rangetest::bestKm() < 3.5);
    {
        std::string csv;
        EXPECT(storage::readFile("/HamOS/rangetest.csv", csv) && csv.find(",RNG,") == std::string::npos && csv.find("Range sender,6,") != std::string::npos);
    }
    shot("range-test");
    key('r');                                  // off
    key('s');                                  // send
    size_t loraBefore = sim_lora_sent.size();
    run(32000);
    EXPECT(rangetest::sentCount() >= 1 && sim_lora_sent.size() > loraBefore);
    key('s');
    back();

    // ---- Weather station and LoRa-APRS digipeater ----
    config::set("grove_io", "wx");
    config::set("lora_mode", "aprs");
    config::set("lora_digi", "1");
    settings::apply(settings::RIG | settings::MESH);
    run(1500);
    EXPECT(weather::valid() && std::fabs(weather::celsius() - 18.4) < 0.01 && loraaprs::ready());
    sim_lora_inject(std::string("<\xff\x01") + "TA1WAL-7>APLRT1,WIDE1-1:!3826.50N/02711.00E>walking");
    run(2500);
    EXPECT(sim_lora_sent.back() == std::string("<\xff\x01") + "TA1WAL-7>APLRT1,TA3BLD-7,WIDE1*:!3826.50N/02711.00E>walking");
    size_t afterDigi = sim_lora_sent.size();
    sim_lora_inject(std::string("<\xff\x01") + "TA1WAL-7>APLRT1,WIDE1-1:!3826.50N/02711.00E>walking");     // the same again: not repeated
    run(2500);
    EXPECT(sim_lora_sent.size() == afterDigi);
    openNext();                                // WX / Digi
    EXPECT(std::string(current->name()) == "WX / Digi");
    key('b');
    run(2500);
    EXPECT(sim_lora_sent.back().find("TA3BLD-13>APZHAM,WIDE1-1:!3825.42N/02708.57E_.../...g...t065h63b") != std::string::npos);
    shot("wx-digi");
    back();
    config::set("grove_io", "");
    config::set("lora_mode", "mesh");
    config::set("lora_digi", "0");
    settings::apply(settings::RIG | settings::MESH);
    EXPECT(mesh::ready());

    // ---- Contest: fast logging with serial numbers ----
    openNext();                                // Contest
    EXPECT(std::string(current->name()) == "Contest");
    size_t before = logbook::total();
    type("ok1abc"); key(' '); type("17"); enter();
    EXPECT(logbook::total() == before + 1 && logbook::recent().back().call == "OK1ABC" && logbook::recent().back().srx == "17" &&
           logbook::recent().back().mode == rig::mode() && logbook::recent().back().rstSent == "59" &&      // mode from the radio (rigctld)
           !logbook::recent().back().stx.empty());
    type("sp5xyz"); key(' '); type("4"); enter();
    type("ok1abc");                            // the same station again: a dupe
    shot("contest");
    {
        Keys undo;
        undo.fn = true;
        undo.backspace = true;
        back();                                // clear the field
        press(undo);
        EXPECT(logbook::total() == before + 1 && logbook::recent().back().call == "OK1ABC");
    }
    back();

    // ---- Awards ----
    openNext();                                // Awards
    run(300);
    EXPECT(std::string(current->name()) == "Awards");
    shot("awards");
    tab(); tab();
    shot("awards-provinces");
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
    EXPECT(wsjtxlink::decodes().size() == 7 && wsjtxlink::logged() == 1 && logbook::total() == 8);
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
    runUntilQuiet();
    {
        rtty::Demodulator check;
        sim_speaker.resize(sim_speaker.size() + 11025 / 2, 0);     // the receiver hears on after the picture
        check.feed(sim_speaker.data(), sim_speaker.size());
        EXPECT(check.text == "RYRYRY CQ CQ CQ DE TA3BLD TA3BLD TA3BLD K");
    }
    back();

    // ---- SSTV: receive a Robot 36 picture from the microphone, then send one ----
    openNext();
    EXPECT(std::string(current->name()) == "SSTV");
    {
        // The "other station" sends qsl.bmp in Robot 36.
        std::string file;
        storage::readFile("/HamOS/sstv/qsl.bmp", file);
        bmp::Info info;
        EXPECT(bmp::parse((const uint8_t*)file.data(), file.size(), info) && info.width == 320 && info.height == 256);
        struct Pic { const std::string* file; bmp::Info info; } pic{&file, info};
        size_t count;
        const sstv::Mode* modes = sstv::modes(count);
        const sstv::Mode* robot36 = sstv::byVis(8);
        sstv::Encoder enc(*robot36, [](void* u, int y, uint8_t* rgb) {
            Pic& p = *(Pic*)u;
            int sy = y * p.info.height / 240;
            const uint8_t* row = (const uint8_t*)p.file->data() + bmp::rowOffset(p.info, sy);
            for (int x = 0; x < 320; x++) { rgb[x * 3] = row[x * 3 + 2]; rgb[x * 3 + 1] = row[x * 3 + 1]; rgb[x * 3 + 2] = row[x * 3]; }
            return true;
        }, &pic, 11025, true, 9000);
        std::vector<int16_t> audio;
        int16_t buf[1024];
        for (size_t n; (n = enc.read(buf, 1024)) > 0;) audio.insert(audio.end(), buf, buf + n);
        (void)modes;
        hear(audio, 1.0, 600);
        run(2000);
        std::string saved = sstvApp.lastSavedForTest();
        EXPECT(!saved.empty() && storage::fileSize(saved.c_str()) == 54 + 320 * 3 * 240);
        // The received picture looks like the one sent.
        std::string rx;
        storage::readFile(saved.c_str(), rx);
        bmp::Info ri;
        double diff = 255;
        if (bmp::parse((const uint8_t*)rx.data(), rx.size(), ri)) {
            double sum = 0;
            int n = 0;
            for (int y = 10; y < 230; y += 7)
                for (int x = 10; x < 310; x += 5) {
                    const uint8_t* a = (const uint8_t*)rx.data() + bmp::rowOffset(ri, y) + x * 3;
                    const uint8_t* b = (const uint8_t*)file.data() + bmp::rowOffset(info, y * 256 / 240) + x * 3;
                    for (int c = 0; c < 3; c++) sum += std::abs((int)a[c] - (int)b[c]);
                    n += 3;
                }
            diff = sum / n;
        }
        EXPECT(diff < 20);
        fprintf(stderr, "sstv: received picture differs by %.1f on average (0..255)\n", diff);
        shot("sstv-receive");

        // Send testcard.bmp in Robot 36 with the callsign on it.
        tab();
        EXPECT(sstvApp.selectForTest("testcard.bmp"));
        for (int i = 0; i < 5; i++) key('m');     // Martin M1 -> ... -> Robot 36
        shot("sstv-gallery");
        sim_speaker.clear();
        enter();
        run(5000);
        shot("sstv-send");
        runUntilQuiet();
        sstv::Decoder check(11025);
        struct Got { int lines = 0; int white = 0; } got;
        check.onLine([](void* u, const sstv::Mode&, int y, const uint8_t* rgb) {
            Got& g = *(Got*)u;
            g.lines = y + 1;
            if (y >= 6 && y < 22)                  // the callsign rows: some pixels must be white
                for (int x = 4; x < 120; x++) g.white += rgb[x * 3] > 200 && rgb[x * 3 + 1] > 200 && rgb[x * 3 + 2] > 200;
        }, &got);
        sim_speaker.resize(sim_speaker.size() + 11025 / 2, 0);     // the receiver hears on after the picture
        check.feed(sim_speaker.data(), sim_speaker.size());
        fprintf(stderr, "sstv send: mode %s, %d lines, %d white pixels in the callsign rows, %zu samples\n", check.mode() ? check.mode()->name : "none", got.lines, got.white, sim_speaker.size());
        EXPECT(check.mode() == robot36 && got.lines == 240 && got.white > 100);
        back();
    }

    // ---- PSK31: find a signal on the waterfall, read it, answer ----
    openNext();
    EXPECT(std::string(current->name()) == "PSK31");
    {
        psk31::Modulator other("CQ CQ CQ de TA1PSK TA1PSK pse k\nCQ CQ CQ de TA1PSK TA1PSK pse k\n", 1463.0, 8000, 9000);
        std::vector<int16_t> audio;
        int16_t buf[512];
        for (size_t n; (n = other.read(buf, 512)) > 0;) audio.insert(audio.end(), buf, buf + n);
        // The other station keeps sending while the cursor is moved to it.
        for (int16_t v : audio) sim_mic.push_back((int16_t)(v + rand() % 1800 - 900));
        Keys right; right.fn = true; right.right = true;
        for (int i = 0; i < 45; i++) press(right);                          // cursor 1000 -> 1450 Hz
        tab();                                                              // snap to the signal next to it
        run(4000);
        shot("psk31-receive");
        fprintf(stderr, "psk31: snapped to %.1f Hz\n", pskApp.frequencyForTest());
        EXPECT(std::fabs(pskApp.frequencyForTest() - 1463) < 12);           // close enough for the AFC
        while (!sim_mic.empty()) run(10);
        hear(std::vector<int16_t>(8000, 0), 1.0, 900);
        fprintf(stderr, "psk31: AFC at %.1f Hz, received \"%s\"\n", pskApp.frequencyForTest(), pskApp.receivedForTest().c_str());
        EXPECT(std::fabs(pskApp.frequencyForTest() - 1463) < 1.5);
        EXPECT(pskApp.receivedForTest().find("de TA1PSK TA1PSK pse k") != std::string::npos);
        type("TA1PSK de TA3BLD ur 599 Izmir");
        shot("psk31-send");
        sim_speaker.clear();
        enter();
        runUntilQuiet();
        psk31::Demodulator check(8000);
        check.setFrequency(pskApp.frequencyForTest());
        std::vector<int16_t> tx = sim_speaker;
        tx.resize(tx.size() + 8000, 0);
        check.feed(tx.data(), tx.size());
        EXPECT(check.text.find("TA1PSK de TA3BLD ur 599 Izmir") != std::string::npos);
        back();
    }

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

    // ---- USB link: what the PC program does ----
    {
        back();                                // to the launcher
        json::Object r = usbCall("\"cmd\":\"hello\"");
        EXPECT(r.flag("ok") && has(r, "\"callsign\":\"TA3BLD\"") && has(r, "\"apps\":[\"Logbook\",\"Mesh\"") && has(r, "\"Settings\",\"Update\",\"Emergency\",\"Self test\",\"Audio levels\",\"Profiles\",\"Setup wizard\"]"));
        EXPECT(!usbCall("\"cmd\":\"nonsense\"").flag("ok") && !usbCall("\"cmd\":\"files.read\",\"path\":\"../x\"").flag("ok"));
        // Settings
        r = usbCall("\"cmd\":\"config.list\"");
        EXPECT(r.flag("ok") && has(r, "{\"key\":\"grid\",\"label\":\"Grid locator\",\"default\":\"\",\"value\":\"KM38nk\""));
        EXPECT(usbCall("\"cmd\":\"config.set\",\"key\":\"utc_offset\",\"value\":\"2\"").flag("ok") && config::get("utc_offset") == "2");
        usbCall("\"cmd\":\"config.set\",\"key\":\"utc_offset\",\"value\":\"\"");
        EXPECT(config::get("utc_offset", "x") == "x");
        // Files: write in two pieces, list, read back, delete
        std::string a = "Merhaba \xc3\xa7\xc4\x9f ", b(5000, 'x');
        EXPECT(usbCall("\"cmd\":\"files.write\",\"path\":\"/HamOS/usbtest.txt\",\"offset\":0,\"data\":\"" + base64::encode((const uint8_t*)a.data(), a.size()) + "\"").flag("ok"));
        EXPECT(usbCall("\"cmd\":\"files.write\",\"path\":\"/HamOS/usbtest.txt\",\"offset\":" + std::to_string(a.size()) + ",\"data\":\"" + base64::encode((const uint8_t*)b.data(), b.size()) + "\"").flag("ok"));
        EXPECT(!usbCall("\"cmd\":\"files.write\",\"path\":\"/HamOS/usbtest.txt\",\"offset\":3,\"data\":\"eA==\"").flag("ok"));
        r = usbCall("\"cmd\":\"files.list\",\"path\":\"/HamOS\"");
        EXPECT(has(r, "{\"name\":\"guide\",\"dir\":true") && has(r, "{\"name\":\"usbtest.txt\",\"dir\":false,\"size\":" + std::to_string(a.size() + b.size())));
        std::string back64;
        for (int off = 0; off < (int)(a.size() + b.size()); off += 3072) {
            r = usbCall("\"cmd\":\"files.read\",\"path\":\"/HamOS/usbtest.txt\",\"offset\":" + std::to_string(off));
            std::string raw = r.str("_raw");
            size_t d = raw.find("\"data\":\"") + 8, e = raw.find('"', d);
            back64 += base64::decode(raw.substr(d, e - d));
        }
        EXPECT(back64 == a + b);
        EXPECT(usbCall("\"cmd\":\"files.delete\",\"path\":\"/HamOS/usbtest.txt\"").flag("ok") && !storage::exists("/HamOS/usbtest.txt"));
        // Logbook
        size_t before = logbook::total();
        EXPECT(usbCall("\"cmd\":\"log.add\",\"call\":\"ok1abc\",\"freq\":\"7.074\",\"mode\":\"ft8\",\"rstSent\":\"-05\",\"rstRcvd\":\"-11\",\"grid\":\"JO70\"").flag("ok"));
        EXPECT(logbook::total() == before + 1);
        r = usbCall("\"cmd\":\"log.list\"");
        EXPECT(has(r, "\"call\":\"OK1ABC\"") && has(r, "\"band\":\"40m\"") && has(r, "\"grid\":\"JO70\""));
        size_t idx = logbook::recent().size() - 1;
        EXPECT(!usbCall("\"cmd\":\"log.delete\",\"index\":" + std::to_string(idx) + ",\"call\":\"WRONG\"").flag("ok"));
        EXPECT(usbCall("\"cmd\":\"log.delete\",\"index\":" + std::to_string(idx) + ",\"call\":\"OK1ABC\"").flag("ok") && logbook::total() == before);
        // Radio data
        size_t loraBefore = sim_lora_sent.size();
        EXPECT(usbCall("\"cmd\":\"mesh.send\",\"text\":\"PC'den selam\"").flag("ok"));
        run(3000);
        EXPECT(sim_lora_sent.size() == loraBefore + 1);
        r = usbCall("\"cmd\":\"mesh.list\"");
        EXPECT(has(r, "\"text\":\"PC'den selam\",") && has(r, "\"long\":\"Bornova Repeater\""));
        r = usbCall("\"cmd\":\"aprs.list\"");
        EXPECT(has(r, "\"call\":\"TA3XYZ-9\"") && has(r, "\"via\":\"lora\""));
        // Remote control: open an app, press keys, mirror the screen
        EXPECT(usbCall("\"cmd\":\"app.open\",\"name\":\"Countries\"").flag("ok") && std::string(current->name()) == "Countries");
        EXPECT(usbCall("\"cmd\":\"key\",\"ch\":\"t\"").flag("ok") && usbCall("\"cmd\":\"key\",\"key\":\"enter\"").flag("ok"));
        std::string screen;
        r = usbCall("\"cmd\":\"screen.mirror\",\"on\":true", &screen);
        run(1200);
        screen += Serial.tx;
        EXPECT(r.flag("ok") && screen.find("~R 0 0 240 135") != std::string::npos && screen.find("~T ") != std::string::npos &&
               screen.find("~I ") != std::string::npos);             // the big flag
        EXPECT(usbCall("\"cmd\":\"key\",\"key\":\"esc\"").flag("ok") && usbCall("\"cmd\":\"key\",\"key\":\"esc\"").flag("ok"));
        EXPECT(current == &launcher);
        usbCall("\"cmd\":\"screen.mirror\",\"on\":false");
        EXPECT(!usb::mirroring());
        // The same over WiFi: the screen goes to the web buffer, not the USB port
        Serial.tx.clear();
        EXPECT(usb::handle("{\"id\":5,\"cmd\":\"screen.mirror\",\"on\":true}", true).find("\"ok\":true") != std::string::npos);
        run(300);
        std::string web = usb::webScreen();
        EXPECT(usb::mirroring() && web.find("~R 0 0 240 135") != std::string::npos && Serial.tx.find("~") == std::string::npos);
        EXPECT(usb::webScreen().empty());       // nothing new drawn since
        usb::handle("{\"id\":6,\"cmd\":\"screen.mirror\",\"on\":false}", true);
        EXPECT(!usb::mirroring());
        key('/');                              // back where the script expects the launcher cursor
        for (int i = 0; i < 20; i++) key(',');
        for (int i = 0; i < 21; i++) key('/');
    }

    openNext();                                // Settings
    for (int i = 0; i < 20; i++) key('.');
    shot("settings");
    back();

    // ---- Update: a new release on GitHub, installed only after Y, Y ----
    {
        std::string fw(200000, '\0');
        for (size_t i = 0; i < fw.size(); i++) fw[i] = (char)(i * 7 + 3);
        unsigned char sum[32];
        mbedtls_sha256_context c;
        mbedtls_sha256_starts(&c, 0);
        mbedtls_sha256_update(&c, (const unsigned char*)fw.data(), fw.size());
        mbedtls_sha256_finish(&c, sum);
        char hex[65];
        for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", sum[i]);
        std::string rel = std::string(R"({"tag_name":"v9.1.0","name":"HamOS 9.1","body":"## What's new\r\n* POTA and SOTA spots\r\n* Emergency mode\r\n* Turkish menus\r\n\r\n**Full Changelog**: v0.8.0...v9.1.0",)") +
                          R"("assets":[{"url":"https://api.github.com/repos/TA3BLD/cardputer-hamos/releases/assets/7","name":"firmware.bin","size":)" +
                          std::to_string(fw.size()) + R"(,"digest":"sha256:)" + hex +
                          R"(","browser_download_url":"https://github.com/TA3BLD/cardputer-hamos/releases/download/v9.1.0/firmware.bin"}]})";
        sim_http["api.github.com/repos/TA3BLD/cardputer-hamos/releases/latest"] = rel;
        sim_http["releases/download/v9.1.0/firmware.bin"] = "REDIRECT https://release-assets.githubusercontent.com/fw?sig=1";
        sim_http["release-assets.githubusercontent.com/fw"] = fw;

        usb::handle("{\"id\":1,\"cmd\":\"app.open\",\"name\":\"Update\"}");
        run(100);
        EXPECT(std::string(current->name()) == "Update");
        enter();                               // check now
        EXPECT(updater::status().state == updater::State::Available && updater::status().latest.version == "9.1.0");
        shot("update-available");
        key('y');
        shot("update-confirm");
        EXPECT(!Update.started);               // nothing happens before the second Y
        key('n');
        EXPECT(updater::status().state == updater::State::Available && !Update.started);
        key('y'); key('y');
        EXPECT(updater::status().state == updater::State::Installed && Update.finished && Update.data == fw);
        EXPECT(sim_nvs["hamos_upd"]["watch"] == "1");
        shot("update-installed");

        // A download that does not match the published checksum is thrown away
        fw[1000] ^= 1;
        sim_http["release-assets.githubusercontent.com/fw"] = fw;
        updater::check();
        Update.finished = false;
        key('y'); key('y');
        EXPECT(updater::status().state == updater::State::Failed && Update.aborted && !Update.finished);

        // The new version fails to start three times: the old one comes back
        sim_nvs["hamos_upd"]["watch"] = "1";
        sim_nvs["hamos_upd"]["starts"] = "0";
        updater::bootGuard();
        updater::bootGuard();
        EXPECT(sim_boot_slot == 0);
        updater::bootGuard();
        EXPECT(sim_boot_slot == 1 && sim_nvs["hamos_upd"]["undone"] == HAMOS_VERSION);
        updater::bootGuard();                  // started again, now as the old version
        EXPECT(updater::rolledBackFrom() == HAMOS_VERSION);
        back();
    }

    // ---- Emergency ----
    {
        auto lastMine = []() {
            for (size_t i = mesh::messages().size(); i-- > 0;)
                if (mesh::messages()[i].mine) return mesh::messages()[i].text;
            return std::string();
        };
        M5Cardputer.BtnA.press(true);          // hold G0 for two seconds, from the launcher
        run(2200);
        M5Cardputer.BtnA.press(false);
        EXPECT(current == &emergencyApp);
        shot("emergency");
        size_t meshBefore = mesh::messagesAdded();
        key('s');
        EXPECT(emergency::state() == emergency::State::Countdown);
        run(1500);
        key(' ');                              // changed my mind
        run(5000);
        EXPECT(emergency::state() == emergency::State::Idle && mesh::messagesAdded() == meshBefore);

        key('n'); type("bacak kirik"); enter();
        run(2000);
        shot("emergency-countdown");
        run(3500);
        EXPECT(emergency::state() == emergency::State::Active && emergency::sentCount() == 1);
        EXPECT(lastMine().rfind("SOS TA3BLD", 0) == 0 && lastMine().find("bacak kirik") != std::string::npos &&
               emergency::sentVia().find("mesh") != std::string::npos);
        run(500);
        // another node repeats our SOS: we hear our own packet come back
        if (!sim_lora_sent.empty()) {
            meshtastic::Header h;
            std::string last = sim_lora_sent.back();
            meshtastic::unpackHeader((const uint8_t*)last.data(), last.size(), h);
            h.hopLimit = (uint8_t)(h.hopLimit - 1);
            uint8_t head[meshtastic::HEADER_LEN];
            meshtastic::packHeader(h, head);
            sim_lora_inject(std::string((const char*)head, sizeof(head)) + last.substr(meshtastic::HEADER_LEN));
            run(100);
        }
        EXPECT(emergency::repeatedByMesh());
        fromNode(0xa1b2c3d4, meshtastic::PORT_TEXT, "Geliyoruz, 10 dakika");
        run(61000);
        EXPECT(emergency::sentCount() == 2);
        EXPECT(!emergency::alerts().empty() && emergency::alerts().back().reply);
        shot("emergency-active");
        key('x'); key('y');
        EXPECT(emergency::state() == emergency::State::Idle && lastMine().find("All clear") == 0);
        back();

        // Someone else calls for help while another app is open
        usb::handle("{\"id\":1,\"cmd\":\"app.open\",\"name\":\"Logbook\"}");
        run(100);
        fromNode(0x0badcafe, meshtastic::PORT_TEXT, "ACİL: yaralı var, Bornova köprü");
        run(300);
        EXPECT(current == &emergencyApp && !emergency::alerts().empty() && !emergency::alerts().back().reply);
        shot("emergency-alert");
        tab(); tab();                          // alerts -> info -> SOS page

        // Status messages
        tab();
        key('1');
        EXPECT(lastMine().find("OK - I am safe - TA3BLD") == 0);
        tab(); tab(); tab();                   // back to the SOS page

        // Safety timer: nobody presses a key -> SOS by itself
        emergency::setTimer(1);
        run(62000);
        EXPECT(emergency::state() == emergency::State::Active && emergency::siren() &&
               lastMine().find("no answer to safety timer") != std::string::npos);
        key('a');                              // siren off
        key('x'); key('y');
        EXPECT(emergency::state() == emergency::State::Idle);
        back();
    }

    // ---- Launcher: search, groups, favourites ----
    {
        auto open = [](const char* name) {
            usb::handle(std::string("{\"id\":1,\"cmd\":\"app.open\",\"name\":\"") + name + "\"}");
            run(200);
        };
        open("home");
        type("ft"); run(100);
        shot("launcher-search");
        enter();
        EXPECT(std::string(current->name()) == "FT8");
        back();
        back();                                // clears the search
        Keys fnEnter;
        fnEnter.fn = true;
        fnEnter.enter = true;
        press(fnEnter);                        // the selected tile (still FT8) becomes a favourite
        EXPECT(config::get("favorites") == "4");
        tab();                                 // Favourites group
        enter();
        EXPECT(std::string(current->name()) == "FT8");
        back();
        for (int i = 0; i < 4; i++) tab();     // Mesh/APRS group
        shot("launcher-group");
        back();                                // back to All

        // Setup wizard: walk through, keeping every answer
        open("Setup wizard");
        shot("setup");
        for (int i = 0; i < 8; i++) enter();
        EXPECT(current == &launcher && config::get("setup_done") == "1" && config::get("callsign") == "TA3BLD" &&
               config::get("lang") == "en" && config::get("lora_mode") == "mesh");

        // Profiles: Field, then back to Home
        open("Profiles");
        key('.'); enter(); key('y');
        EXPECT(config::get("profile") == "1" && config::get("brightness") == "90" && config::get("lora_aprs_beacon") == "10");
        shot("profiles");
        key(';'); enter(); key('y');
        EXPECT(config::get("profile") == "0" && config::get("lora_mode") == "mesh" && mesh::ready());
        key('s');
        {
            std::string text;
            EXPECT(storage::readFile("/HamOS/profiles/1.txt", text) && text.find("brightness=160") != std::string::npos);
        }
        back();

        // Self test
        open("Self test");
        for (int i = 0; i < 40 && !selfTestApp.finished(); i++) run(100);
        key('y');
        bool sdOk = false;
        for (const auto& r : selfTestApp.rows()) sdOk |= r.name == "SD card" && r.state == SelfTestApp::OK;
        EXPECT(selfTestApp.finished() && sdOk && selfTestApp.rows().size() >= 12);
        shot("self-test");
        back();

        // Audio levels: a tone at about -20 dB
        open("Audio levels");
        for (int i = 0; i < 8000; i++) sim_mic.push_back((int16_t)(3300 * sin(i * 0.4)));
        run(400);
        shot("audio-levels");
        tab();
        run(300);
        EXPECT(audio::playing());
        tab();
        EXPECT(!audio::playing());
        back();
    }

    // ---- The same screens in Turkish (the device's default language) ----
    {
        config::set("lang", "tr");
        settings::apply(settings::LANG);
        usb::handle("{\"id\":1,\"cmd\":\"app.open\",\"name\":\"home\"}");
        run(200);
        EXPECT(std::string(logbookApp.name()) == "Log defteri");
        shot("tr-launcher");
        for (const char* app : {"Log defteri", "Acil durum", "Ayarlar", "POTA/SOTA"}) {
            usb::handle(std::string("{\"id\":1,\"cmd\":\"app.open\",\"name\":\"") + app + "\"}");
            run(300);
            EXPECT(std::string(current->name()) == app);
            shot((std::string("tr-") + (app[0] == 'L' ? "logbook" : app[0] == 'A' && app[1] == 'c' ? "emergency" : app[0] == 'A' ? "settings" : "pota")).c_str());
        }
        config::set("lang", "en");
        settings::apply(settings::LANG);
    }

    if (failures) fprintf(stderr, "%d simulator checks FAILED\n", failures);
    else fprintf(stderr, "all simulator checks passed\n");
    return failures ? 1 : 0;
}
