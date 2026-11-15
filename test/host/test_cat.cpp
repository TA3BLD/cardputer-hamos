// Host test for the radio control protocols (lib/Cat.h).
//   g++ -std=c++17 -I src test/host/test_cat.cpp -o test/host/t.out && test/host/t.out
#include <cstdio>
#include "lib/Cat.h"
#include "lib/Iambic.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

using bytes = std::vector<uint8_t>;

int main() {
    using namespace cat;
    // CI-V: 14.074 MHz is 00 40 07 14 00 (Icom manual example style), IC-7300 address 94
    CHECK(civ::setFreq(0x94, 14074000) == bytes({0xFE, 0xFE, 0x94, 0xE0, 0x05, 0x00, 0x40, 0x07, 0x14, 0x00, 0xFD}));
    CHECK(civ::setFreq(0xA4, 145500000) == bytes({0xFE, 0xFE, 0xA4, 0xE0, 0x05, 0x00, 0x00, 0x50, 0x45, 0x01, 0xFD}));
    CHECK(civ::readFreq(0x94) == bytes({0xFE, 0xFE, 0x94, 0xE0, 0x03, 0xFD}));
    CHECK(civ::setMode(0x94, civ::modeCode("SSB", 7.1)) == bytes({0xFE, 0xFE, 0x94, 0xE0, 0x06, 0x00, 0x01, 0xFD}));
    CHECK(civ::ptt(0x94, true) == bytes({0xFE, 0xFE, 0x94, 0xE0, 0x1C, 0x00, 0x01, 0xFD}));
    CHECK(civ::sendCw(0x94, "cq de") == bytes({0xFE, 0xFE, 0x94, 0xE0, 0x17, 'C', 'Q', ' ', 'D', 'E', 0xFD}));

    civ::Reader r;
    // Our own request echoed on the one-wire bus, then the radio's answers.
    bytes stream = {0xFE, 0xFE, 0x94, 0xE0, 0x03, 0xFD,
                    0xFE, 0xFE, 0xE0, 0x94, 0x03, 0x00, 0x40, 0x07, 0x14, 0x00, 0xFD,
                    0xFE, 0xFE, 0xE0, 0x94, 0x04, 0x03, 0x01, 0xFD,
                    0xFE, 0xFE, 0xE0, 0x94, 0xFB, 0xFD};
    int changes = 0;
    for (uint8_t b : stream) changes += r.feed(b);
    CHECK(changes == 2 && r.freqHz == 14074000 && r.mode == "CW" && r.ok);
    // Transceive broadcast to address 00, with a noise byte before it
    bytes bc = {0x13, 0xFE, 0xFE, 0x00, 0x94, 0x00, 0x00, 0x50, 0x07, 0x07, 0x00, 0xFD};
    for (uint8_t b : bc) r.feed(b);
    CHECK(r.freqHz == 7075000);

    // FT-817: 14.250.00 MHz is 01 42 50 00 (Yaesu manual)
    CHECK(ft817::setFreq(14250000) == bytes({0x01, 0x42, 0x50, 0x00, 0x01}));
    uint8_t ans[5] = {0x01, 0x40, 0x74, 0x00, 0x0A};
    uint64_t hz = 0;
    std::string mode;
    CHECK(ft817::parse(ans, hz, mode) && hz == 14074000 && mode == "DATA");
    CHECK(ft817::setMode("CW", 7) == bytes({0x02, 0, 0, 0, 0x07}));

    // Kenwood / Yaesu ASCII
    CHECK(ascii::setFreq(14074000, 11) == "FA00014074000;" && ascii::setFreq(7074000, 9) == "FA007074000;");
    CHECK(ascii::setModeKenwood("CW", 7) == "MD3;" && ascii::setModeYaesu("FT8", 14) == "MD0C;");
    CHECK(ascii::sendCw("cq test").size() == 2 + 1 + 24 + 1);
    ascii::Reader k;
    for (char c : std::string("FA00014074000;MD2;")) k.feed(c);
    CHECK(k.freqHz == 14074000 && k.digits == 11 && k.mode == "SSB");
    ascii::Reader y;
    y.yaesu = true;
    for (char c : std::string("FA007074000;MD0C;")) y.feed(c);
    CHECK(y.freqHz == 7074000 && y.digits == 9 && y.mode == "DATA");

    // IC-705 handshake
    std::string id = ic705::deviceId(0x78563412AABBULL);
    CHECK(id.size() == 36 && id == "48414D4F-5300-4000-8000-78563412AABB");
    bytes hello = ic705::sendId(id);
    CHECK(hello.size() == 41 && hello[3] == 0x61 && hello.back() == 0xFD);
    uint8_t granted[] = {0xFE, 0xF1, 0x00, 0x64, 0xFD};
    CHECK(ic705::handshakeReply(granted, 5) == 0x64);

    // Iambic keyer (mode B): hold dit for "S", dah for "T"; squeeze for "C"
    {
        morse::Iambic k;
        k.setWpm(20);                           // dot 60 ms
        uint32_t t = 0;
        auto run = [&](uint32_t ms, bool dit, bool dah, std::string* keyed = nullptr) {
            bool prev = false;
            for (uint32_t end = t + ms; t < end; t += 2) {
                bool d = k.update(t, dit, dah);
                if (keyed && d && !prev) *keyed += "+";
                prev = d;
            }
        };
        std::string keyed;
        run(300, true, false, &keyed);          // dit held 300 ms: 3 dits (120 ms each)... with gaps
        run(400, false, false);
        run(170, false, true);                  // one dah (180 ms) then release
        run(500, false, false);
        CHECK(k.text() == "ST ");
        k.take();
        // squeeze, dah first, let go during the second dah: mode B adds the dit -> dah dit dah dit = C
        run(10, false, true);
        run(380, true, true);
        run(500, false, false);
        CHECK(k.take().substr(0, 1) == "C");
        // A word gap makes a space
        run(30, true, false);
        run(700, false, false);
        CHECK(k.text() == "E ");
    }

    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
