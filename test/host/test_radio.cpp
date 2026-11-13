// Host-side test (needs OpenSSL for AES):
//   g++ -std=c++17 -I src test/host/test_radio.cpp -lcrypto -o test/host/t.out && test/host/t.out
// With an argument it prints a sample encrypted packet as hex, used by
// tools/meshtastic_crosscheck.py to compare against the official Python library.
#include <openssl/aes.h>
#include <cstdio>
#include "lib/DxSpot.h"
#include "lib/Dxcc.h"
#include "lib/Meshtastic.h"
#include "lib/Nmea.h"
#include "lib/SolarXml.h"
#include "lib/Sun.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) < tol; }

struct OpenSslAes : meshtastic::BlockCipher {
    AES_KEY key;
    OpenSslAes(const uint8_t* k, int len) { AES_set_encrypt_key(k, len * 8, &key); }
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) override { AES_encrypt(in, out, &key); }
};

static std::string country(const char* call) {
    const dxcc::Entity* e = dxcc::lookup(call);
    return e ? e->name : "?";
}

int main(int argc, char**) {
    using namespace meshtastic;

    // ---- Meshtastic: keys, channel hash, frequencies ----
    uint8_t key[32];
    CHECK(expandPsk("AQ==", key) == 16 && memcmp(key, kDefaultKey, 16) == 0);
    CHECK(channelHash("LongFast", key, 16) == 0x08);            // well-known value for the default channel
    CHECK(expandPsk("Ag==", key) == 16 && key[15] == 0x02 && key[0] == 0xd4);
    CHECK(expandPsk("", key) == 0 && expandPsk("AA==", key) == 0);
    CHECK(expandPsk("1PG7OiApB1nwvP+rz05pAQ==", key) == 16 && memcmp(key, kDefaultKey, 16) == 0);
    CHECK(expandPsk("AAEC", key) == -1);                        // 3 bytes: not a valid key

    const Preset* lf = findPreset("LongFast");
    CHECK(lf && lf->sf == 11 && lf->bwKhz == 250 && findPreset("nope") == nullptr);
    CHECK(near(channelFrequencyMhz(*findRegion("EU_868"), *lf, "LongFast"), 869.525, 1e-6));
    CHECK(near(channelFrequencyMhz(*findRegion("US"), *lf, "LongFast"), 906.875, 1e-6));
    CHECK(near(channelFrequencyMhz(*findRegion("EU_433"), *lf, "LongFast"), 433.875, 1e-6));
    double toa = timeOnAirMs(*lf, 40);
    CHECK(toa > 500 && toa < 900);                              // ~0.7 s for a short text on LongFast

    // ---- Meshtastic: header ----
    Header h;
    h.to = BROADCAST; h.from = 0x12345678; h.id = 0xCAFEBABE; h.hopLimit = 3; h.hopStart = 3; h.wantAck = true;
    h.channel = 8;
    uint8_t packet[MAX_PACKET];
    packHeader(h, packet);
    CHECK(packet[4] == 0x78 && packet[7] == 0x12 && packet[12] == (3 | 0x08 | (3 << 5)) && packet[13] == 8);
    Header h2;
    CHECK(unpackHeader(packet, 16, h2) && h2.from == h.from && h2.id == h.id && h2.wantAck && h2.hopStart == 3);
    CHECK(!unpackHeader(packet, 15, h2));

    // ---- Meshtastic: protobuf round trips ----
    Data d;
    d.portnum = PORT_TEXT;
    d.payload = "73 de TA3BLD";
    std::string plain = encodeData(d);
    Data d2;
    CHECK(decodeData(plain, d2) && d2.portnum == PORT_TEXT && d2.payload == d.payload && !d2.wantResponse);
    CHECK(!decodeData(std::string("\xff\xff\xff", 3), d2));     // garbage must be rejected
    CHECK(!decodeData("", d2));

    User u;
    u.id = "!12345678"; u.longName = "TA3BLD HamOS"; u.shortName = "BLD";
    User u2;
    CHECK(decodeUser(encodeUser(u), u2) && u2.longName == u.longName && u2.shortName == "BLD" && u2.hwModel == 255);

    // Byte-for-byte equal to what the official Meshtastic Python library produces.
    auto hex = [](const std::string& b) { std::string o; char t[3]; for (unsigned char c : b) { snprintf(t, 3, "%02x", c); o += t; } return o; };
    CHECK(hex(plain) == "0801120c373320646520544133424c44");
    CHECK(hex(encodeUser(u)) == "0a09213132333435363738120c544133424c442048616d4f531a03424c4428ff013801");

    Position p;
    p.hasLatLon = true; p.lat = 38.4237; p.lon = 27.1428; p.altitude = 120; p.time = 1794000000;
    CHECK(hex(encodePosition(p)) == "0dc8fde61615a0a92d101878258044ee6a");
    Position p2;
    CHECK(decodePosition(encodePosition(p), p2) && p2.hasLatLon && near(p2.lat, p.lat, 1e-6) && near(p2.lon, p.lon, 1e-6));
    CHECK(p2.altitude == 120 && p2.time == p.time);
    p.lat = -33.87; p.lon = -70.5; p.altitude = -5;
    CHECK(decodePosition(encodePosition(p), p2) && near(p2.lat, -33.87, 1e-6) && near(p2.lon, -70.5, 1e-6) && p2.altitude == -5);
    p.precisionBits = 13;                                        // coarse: within a few km, never exact
    CHECK(decodePosition(encodePosition(p), p2) && near(p2.lat, -33.87, 0.05) && p2.lat != -33.87 && p2.precisionBits == 13);

    // ---- Meshtastic: encryption ----
    OpenSslAes aes(kDefaultKey, 16);
    size_t n = plain.size();
    memcpy(packet + HEADER_LEN, plain.data(), n);
    cryptCtr(aes, h.from, h.id, packet + HEADER_LEN, n);
    CHECK(memcmp(packet + HEADER_LEN, plain.data(), n) != 0);
    if (argc > 1) {                                              // sample for the Python cross-check
        for (size_t i = 0; i < HEADER_LEN + n; i++) printf("%02x", packet[i]);
        printf("\n");
        return 0;
    }
    cryptCtr(aes, h.from, h.id, packet + HEADER_LEN, n);
    CHECK(memcmp(packet + HEADER_LEN, plain.data(), n) == 0);
    std::string big(100, 'x');                                   // several AES blocks
    std::string enc = big;
    cryptCtr(aes, 1, 2, (uint8_t*)&enc[0], enc.size());
    CHECK(enc.substr(0, 16) != enc.substr(16, 16));              // counter advances between blocks
    cryptCtr(aes, 1, 2, (uint8_t*)&enc[0], enc.size());
    CHECK(enc == big);

    // ---- NMEA ----
    nmea::Parser gps;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230326,003.1,W*63\r\n";
    bool updated = false;
    for (const char* c = gga; *c; c++) updated |= gps.feed(*c);
    CHECK(updated && gps.fix.valid && near(gps.fix.lat, 48.1173, 1e-4) && near(gps.fix.lon, 11.5167, 1e-4));
    CHECK(gps.fix.satellites == 8 && near(gps.fix.altitudeM, 545.4, 1e-6) && !gps.fix.timeValid);
    for (const char* c = rmc; *c; c++) gps.feed(*c);
    CHECK(gps.fix.timeValid && gps.fix.year == 2026 && gps.fix.month == 3 && gps.fix.day == 23 && gps.fix.hour == 12);
    CHECK(near(gps.fix.speedKmh, 22.4 * 1.852, 1e-6));
    CHECK(!gps.parse("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48"));   // bad checksum
    CHECK(gps.parse("$GNRMC,,V,,,,,,,,,,N*4D") && !gps.fix.valid);                             // no fix yet
    CHECK(!gps.parse("garbage") && !gps.parse("$*") && gps.sentences == 3);

    // ---- Sunrise / sunset: Izmir, 21 June 2026 (05:49 / 20:37 local, UTC+3) ----
    sun::Times s = sun::compute(2026, 6, 21, 38.42, 27.14);
    CHECK(s.rises && near(s.sunriseUtc, 2.0 + 49.0 / 60, 0.1) && near(s.sunsetUtc, 17.0 + 37.0 / 60, 0.1));
    CHECK(!sun::compute(2026, 6, 21, 80, 20).rises);            // midnight sun

    // ---- Solar feed ----
    solar::Data sd = solar::parse("<solar><solardata><updated> 13 Nov 2026 1105 GMT</updated><solarflux>148</solarflux>"
                                  "<aindex> 8</aindex><kindex>2</kindex><xray>B7.4</xray><sunspots>112</sunspots>"
                                  "<band name=\"80m-40m\" time=\"day\">Fair</band><band name=\"12m-10m\" time=\"night\">Poor</band>"
                                  "</solardata></solar>");
    CHECK(sd.valid && sd.flux == "148" && sd.aIndex == "8" && sd.kIndex == "2" && sd.xray == "B7.4");
    CHECK(sd.cond[0][0] == "Fair" && sd.cond[3][1] == "Poor" && sd.cond[1][0].empty());
    CHECK(!solar::parse("<html>error</html>").valid && !solar::parse("").valid);

    // ---- DX spots ----
    dxspot::Spot spot;
    CHECK(dxspot::parse("DX de EA5XX:     14025.0  JA1ABC       CW 599 up 1                1234Z", spot));
    CHECK(spot.spotter == "EA5XX" && spot.call == "JA1ABC" && near(spot.khz, 14025.0, 1e-9));
    CHECK(spot.comment == "CW 599 up 1" && spot.time == "1234Z");
    CHECK(dxspot::parse("DX de W3LPL-#:  7012.5  TA3BLD", spot) && spot.call == "TA3BLD" && spot.time.empty());
    CHECK(!dxspot::parse("To ALL de K1ABC: hello", spot) && !dxspot::parse("DX de X:", spot));

    // ---- Callsign -> country ----
    CHECK(country("TA3BLD") == "Turkey" && country("dl1abc") == "Germany" && country("W1AW") == "USA");
    CHECK(country("KL7AA") == "Alaska" && country("KH6XX") == "Hawaii" && country("JA1ABC") == "Japan");
    CHECK(country("UA9XYZ") == "Asiatic Russia" && country("UA3ABC") == "European Russia" && country("R0AA") == "Asiatic Russia");
    CHECK(country("UR5XX") == "Ukraine" && country("UN7AB") == "Kazakhstan");
    CHECK(country("G4XYZ") == "England" && country("GM4XYZ") == "Scotland" && country("M0ABC") == "England");
    CHECK(country("EA8XX") == "Canary Is." && country("EA5XX") == "Spain" && country("VK2AB") == "Australia");
    CHECK(country("9A1A") == "Croatia" && country("4X1AB") == "Israel" && country("PY2XX") == "Brazil");
    CHECK(country("BV2AA") == "Taiwan" && country("BY1AA") == "China" && country("FK8AA") == "New Caledonia");
    CHECK(country("DL/TA3BLD") == "Germany" && country("TA3BLD/P") == "Turkey" && country("EA8/DL1ABC/P") == "Canary Is.");
    CHECK(country("QQ1AA") == "?" && country("") == "?" && country("/") == "?");

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
