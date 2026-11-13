// Host-side test: g++ -std=c++17 -I src test/host/test_modem.cpp -o test/host/t.out && test/host/t.out
#include <cstdio>
#include <cstdlib>
#include "lib/Aprs.h"
#include "lib/Ax25.h"
#include "lib/CwDecoder.h"
#include "lib/Rtty.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) < tol; }

// Makes a clean signal look like a microphone recording: quieter, with hiss and a DC offset.
static std::vector<int16_t> degrade(const std::vector<int16_t>& in, double gain, int noise, int dc = 0) {
    std::vector<int16_t> out;
    for (int16_t s : in) out.push_back((int16_t)(s * gain + (noise ? rand() % (2 * noise) - noise : 0) + dc));
    return out;
}
template <class D> static void feedChunks(D& d, const std::vector<int16_t>& audio) {
    for (size_t i = 0; i < audio.size(); i += 256) d.feed(audio.data() + i, std::min<size_t>(256, audio.size() - i));
}

int main() {
    srand(1);
    // ---- APRS text formats ----
    CHECK(aprs::passcode("N0CALL") == 13023 && aprs::passcode("n0call-9") == 13023);
    aprs::Packet pk;
    CHECK(aprs::parseTnc2("TA3XYZ-9>APRS,WIDE1-1,qAR,TA3ABC:!4903.50N/07201.75W-Test 123\r\n", pk));
    CHECK(pk.src == "TA3XYZ-9" && pk.dst == "APRS" && pk.path == "WIDE1-1,qAR,TA3ABC" && pk.info == "!4903.50N/07201.75W-Test 123");
    aprs::Position pos = aprs::parsePosition(pk.info);
    CHECK(!aprs::parseTnc2("# aprsc 2.1 server comment", pk) && !aprs::parseTnc2("A>B:", pk));
    CHECK(pos.valid && near(pos.lat, 49.0583, 1e-3) && near(pos.lon, -72.0292, 1e-3) && pos.symbol == '-' && pos.comment == "Test 123");
    pos = aprs::parsePosition("@092345z3825.42N/02708.57E>mobile");
    CHECK(pos.valid && near(pos.lat, 38.4237, 1e-3) && near(pos.lon, 27.1428, 1e-3) && pos.symbol == '>' && pos.comment == "mobile");
    pos = aprs::parsePosition("=/5L!!<*e7>7P[");                       // compressed example from the APRS spec
    CHECK(pos.valid && near(pos.lat, 49.5, 1e-3) && near(pos.lon, -72.75, 1e-3) && pos.symbol == '>');
    CHECK(!aprs::parsePosition(">status text").valid && !aprs::parsePosition("!99xx.50N/07201.75W-").valid &&
          !aprs::parsePosition("!").valid && !aprs::parsePosition("").valid);
    CHECK(aprs::formatPosition(38.4237, 27.1428, '/', '[', "HamOS") == "!3825.42N/02708.57E[HamOS");
    pos = aprs::parsePosition(aprs::formatPosition(-33.8688, -70.6693, '/', '-', ""));
    CHECK(pos.valid && near(pos.lat, -33.8688, 1e-3) && near(pos.lon, -70.6693, 1e-3));

    aprs::Message m = aprs::parseMessage(":TA3BLD   :hello there{12");
    CHECK(m.valid && m.to == "TA3BLD" && m.text == "hello there" && m.id == "12" && !m.isAck);
    m = aprs::parseMessage(":TA3BLD-7 :ack12");
    CHECK(m.valid && m.isAck && m.id == "12" && m.to == "TA3BLD-7");
    m = aprs::parseMessage(":TA3BLD   :no id here");
    CHECK(m.valid && m.id.empty() && m.text == "no id here");
    CHECK(!aprs::parseMessage("!4903.50N/07201.75W-").valid && !aprs::parseMessage(":short").valid);
    CHECK(aprs::formatMessage("TA1ABC", "73", "5") == ":TA1ABC   :73{5" && aprs::formatAck("TA1ABC", "5") == ":TA1ABC   :ack5");

    // ---- AX.25 frames ----
    CHECK(ax25::crc((const uint8_t*)"123456789", 9) == 0x906E);
    const std::string tnc2 = "TA3BLD-7>APZHAM,WIDE1-1,WIDE2-1:!3825.42N/02708.57E[HamOS test";
    std::string frame, back;
    CHECK(ax25::encodeFrame(tnc2, frame) && ax25::decodeFrame((const uint8_t*)frame.data(), frame.size(), back) && back == tnc2);
    std::string bad = frame;
    bad[20] ^= 0x01;
    CHECK(!ax25::decodeFrame((const uint8_t*)bad.data(), bad.size(), back));
    CHECK(!ax25::encodeFrame("no route here", frame) && !ax25::decodeFrame((const uint8_t*)"x", 1, back));

    // ---- AFSK 1200 modem: clean, noisy/quiet/offset, two packets in a row, damaged ----
    ax25::encodeFrame(tnc2, frame);
    std::vector<int16_t> audio = ax25::modulate(frame);
    {
        ax25::Demodulator d;
        feedChunks(d, audio);
        CHECK(d.packets.size() == 1 && d.packets[0] == tnc2);
    }
    {
        ax25::Demodulator d;
        std::string frame2;
        ax25::encodeFrame("TA1ABC>APRS::TA3BLD-7 :selam{7", frame2);     // stuffing-heavy content follows too
        std::vector<int16_t> two = degrade(audio, 0.25, 600, 800);
        std::vector<int16_t> second = degrade(ax25::modulate(frame2), 0.25, 600, 800);
        two.insert(two.end(), second.begin(), second.end());
        feedChunks(d, two);
        CHECK(d.packets.size() == 2 && d.packets[0] == tnc2 && d.packets[1] == "TA1ABC>APRS::TA3BLD-7 :selam{7");
    }
    {
        ax25::Demodulator d;
        std::string ff;
        ax25::encodeFrame("A>B:\xff\xff\xff\xff\x7e\x7e\x7e\xff", ff);    // long runs of ones: bit stuffing
        feedChunks(d, ax25::modulate(ff));
        CHECK(d.packets.size() == 1 && d.packets[0] == "A>B:\xff\xff\xff\xff\x7e\x7e\x7e\xff");
    }
    {
        ax25::Demodulator d;
        std::vector<int16_t> broken = audio;
        for (size_t i = broken.size() / 2; i < broken.size() / 2 + 400; i++) broken[i] = 0;     // dropout
        feedChunks(d, broken);
        feedChunks(d, degrade(audio, 0, 3000));                                               // pure noise
        CHECK(d.packets.empty());
    }

    // ---- RTTY ----
    const std::string msg = "RYRYRY CQ CQ DE TA3BLD TA3BLD PSE K 599 73!";
    {
        rtty::Demodulator d;
        feedChunks(d, rtty::modulate(msg));
        CHECK(d.text == msg);
    }
    {
        rtty::Demodulator d;
        feedChunks(d, degrade(rtty::modulate(msg), 0.3, 700, -500));
        CHECK(d.text == msg && d.level > 0.5);
    }
    {
        rtty::Demodulator d;                   // silence and noise produce nothing much
        feedChunks(d, std::vector<int16_t>(8000, 0));
        CHECK(d.text.empty());
    }
    {
        rtty::Demodulator wrong, right;         // inverted tones: garbage unless "reverse" is on
        std::vector<int16_t> a = rtty::modulate("TEST 123");
        // Swap the tones by modulating with exchanged frequencies is equivalent to reverse=true on a normal signal.
        right.reverse = false;
        feedChunks(right, a);
        wrong.reverse = true;
        feedChunks(wrong, a);
        CHECK(right.text == "TEST 123" && wrong.text != "TEST 123");
    }

    // ---- CW decoder at several speeds, clean and noisy ----
    for (int wpm : {12, 20, 28}) {
        cw::Decoder d;
        feedChunks(d, cw::synthesize("VVV CQ CQ DE TA3BLD K", wpm));
        bool ok = d.text.find("CQ CQ DE TA3BLD K") != std::string::npos && std::abs(d.wpm() - wpm) <= 2;
        if (!ok) printf("  %d WPM clean: \"%s\" (%d WPM)\n", wpm, d.text.c_str(), d.wpm());
        CHECK(ok);
    }
    {
        cw::Decoder d;
        feedChunks(d, degrade(cw::synthesize("VVV TEST DE TA3BLD 599 73", 18), 0.2, 500, 300));
        bool ok = d.text.find("TEST DE TA3BLD 599 73") != std::string::npos;
        if (!ok) printf("  noisy: \"%s\"\n", d.text.c_str());
        CHECK(ok);
    }
    {
        // The way the app uses it: text is taken out after every chunk, and
        // listening starts in the middle of a transmission.
        cw::Decoder d;
        std::vector<int16_t> audio = degrade(cw::synthesize("VVV VVV CQ CQ DE TA3BLD K", 20), 0.3, 400);
        audio.erase(audio.begin(), audio.begin() + 4000);
        std::string collected;
        for (size_t i = 0; i < audio.size(); i += 256) {
            d.feed(audio.data() + i, std::min<size_t>(256, audio.size() - i));
            collected += d.text;
            d.text.clear();
        }
        bool ok = collected.find("CQ CQ DE TA3BLD K") != std::string::npos;
        if (!ok) printf("  mid-signal start: \"%s\"\n", collected.c_str());
        CHECK(ok);
    }
    {
        cw::Decoder d;                          // noise only: nothing decoded
        feedChunks(d, degrade(std::vector<int16_t>(40000, 0), 0, 800));
        if (!d.text.empty()) printf("  noise decoded as \"%s\"\n", d.text.c_str());
        CHECK(d.text.size() <= 1);
    }

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
