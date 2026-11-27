// Host test for the web API helpers: JSON tree reader and GitHub release parsing.
//   g++ -std=c++17 -I src test/host/test_web.cpp -o test/host/t.out && test/host/t.out
#include <cassert>
#include <cstdio>
#include "lib/JsonDom.h"
#include "lib/Release.h"
#include "lib/MiniXml.h"
#include "lib/Csv.h"
#include "lib/PskReport.h"
#include "lib/Cabrillo.h"
#include <cstring>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static std::vector<pskreport::Spot> sampleSpots() {
    std::vector<pskreport::Spot> v;
    v.push_back({"DL1ABC", "JO31", "FT8", 14075123, -12, 1700000000});
    v.push_back({"JA1XYZ", "", "FT8", 14074800, 3, 1700000015});
    v.push_back({"TA2ZZ", "KM69ab", "PSK31", 7040500, -20, 1700000030});
    return v;
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "ipfix")) {     // for tools/pskreporter/check_ipfix.py
        std::vector<uint8_t> b;
        pskreport::build(b, sampleSpots(), 0, true, "TA3BLD", "KM38NK", "HamOS 0.8.0", 1700000100, 0, 0x12345678);
        for (uint8_t c : b) printf("%02x", c);
        printf("\n");
        return 0;
    }
    jdom::Value v;
    CHECK(jdom::parse(R"({"a":1,"b":[true,false,null,"xç😀"],"c":{"d":-2.5e1},"e":"14074.5"})", v));
    CHECK(v["a"].num() == 1 && v["b"].size() == 4 && v["b"][0].flag() && !v["b"][1].flag() && v["b"][2].isNull());
    CHECK(v["b"][3].str() == "x\xc3\xa7\xf0\x9f\x98\x80");
    CHECK(v["c"]["d"].num() == -25 && v["e"].num() == 14074.5 && v["a"].str() == "1");
    CHECK(v["missing"]["deeper"][3].isNull() && v["missing"].str("def") == "def");
    CHECK(!jdom::parse("{\"a\":1,}", v) && !jdom::parse("[1 2]", v) && !jdom::parse("\"open", v) && !jdom::parse("", v));
    CHECK(jdom::parse(" [ ] ", v) && v.isArray() && v.size() == 0);
    std::string deep(40, '[');
    CHECK(!jdom::parse(deep + std::string(40, ']'), v));       // too deep: refused, no stack overflow

    CHECK(release::compareVersions("0.8.0", "v0.9.0") < 0);
    CHECK(release::compareVersions("v1.2.10", "1.2.9") > 0);
    CHECK(release::compareVersions("1.0", "1.0.0") == 0);
    CHECK(release::compareVersions("1.0.0-beta", "1.0.0") < 0);
    CHECK(release::compareVersions("1.0.1-beta", "1.0.0") > 0);

    const char* gh = R"({"url":"https://api.github.com/repos/o/r/releases/1","tag_name":"v0.9.0","name":"HamOS 0.9",
      "prerelease":false,"body":"## What's new\r\n* POTA","assets":[
      {"url":"https://api.github.com/repos/o/r/releases/assets/11","name":"hamos-full.bin","size":3000000,"browser_download_url":"https://github.com/o/r/releases/download/v0.9.0/hamos-full.bin"},
      {"url":"https://api.github.com/repos/o/r/releases/assets/12","name":"firmware.bin","size":1834512,
       "digest":"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
       "uploader":{"login":"x","id":5},"browser_download_url":"https://github.com/o/r/releases/download/v0.9.0/firmware.bin"}]})";
    release::Info info;
    CHECK(release::parse(gh, info));
    CHECK(info.version == "0.9.0" && info.size == 1834512 && info.name == "HamOS 0.9");
    CHECK(info.assetApiUrl == "https://api.github.com/repos/o/r/releases/assets/12");
    CHECK(info.sha256 == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    CHECK(info.notes.find("POTA") != std::string::npos);
    CHECK(!release::parse(R"({"message":"Not Found"})", info));
    CHECK(!release::parse(R"({"tag_name":"v1","assets":[]})", info));
    // The release file names: hamOS_0x0.bin (complete image) is never the update, hamOS_1_1.bin is
    const char* named = R"({"tag_name":"v1.1.0","name":"HamOS v1.1.0","assets":[
      {"url":"https://api.github.com/repos/o/r/releases/assets/21","name":"hamOS_0x0.bin","size":2670048,"browser_download_url":"https://github.com/o/r/releases/download/v1.1.0/hamOS_0x0.bin"},
      {"url":"https://api.github.com/repos/o/r/releases/assets/22","name":"hamOS_1_1.bin","size":2615264,"browser_download_url":"https://github.com/o/r/releases/download/v1.1.0/hamOS_1_1.bin"}]})";
    CHECK(release::parse(named, info) && info.version == "1.1.0" && info.size == 2615264 && info.downloadUrl.find("hamOS_1_1.bin") != std::string::npos);
    CHECK(!release::parse(R"({"tag_name":"v1.1.0","assets":[{"name":"hamOS_0x0.bin","size":5,"browser_download_url":"x"}]})", info));
    CHECK(release::isAppAsset("HamOS_2_0_1.bin") && !release::isAppAsset("hamos-full.bin") && !release::isAppAsset("hamOS_1_1.zip"));

    // XML
    std::string hq = "<?xml version=\"1.0\"?><HamQTH version=\"2.8\"><search><callsign>ok7an</callsign><nick>Petr</nick>"
                     "<qth>Neratovice</qth><grid>jo70ga</grid><adr_name>Petr &amp; Co &#246;</adr_name></search></HamQTH>";
    CHECK(xml::text(hq, "nick") == "Petr" && xml::text(hq, "grid") == "jo70ga" && xml::text(hq, "error").empty());
    CHECK(xml::text(hq, "adr_name") == "Petr & Co \xc3\xb6" && xml::text(hq, "CALLSIGN") == "ok7an");
    std::string pr = "<receptionReports currentSeconds=\"1\"><activeReceiver callsign=\"X\"/>"
                     "<receptionReport receiverCallsign=\"DL1ABC\" receiverLocator=\"JO31le\" senderCallsign=\"TA3BLD\" "
                     "frequency=\"14075123\" flowStartSeconds=\"1700000000\" mode=\"FT8\" sNR=\"-12\" />\n"
                     "<receptionReport receiverCallsign='K1ABC' receiverLocator='FN42' frequency='14075200' sNR='-20' mode='FT8'/>"
                     "<receptionReportsX a=\"1\"/></receptionReports>";
    auto reps = xml::elements(pr, "receptionReport");
    CHECK(reps.size() == 2 && reps[0]["receiverCallsign"] == "DL1ABC" && reps[0]["sNR"] == "-12" && reps[1]["receiverLocator"] == "FN42");

    // CSV
    auto f = csv::split("\"US-0001\",\"Acadia, \"\"NP\"\"\",1,291,\"US-ME\",44.31,-68.2034,FN54vh\r");
    CHECK(f.size() == 8 && f[1] == "Acadia, \"NP\"" && f[7] == "FN54vh");
    CHECK(csv::field("a,b") == "\"a,b\"" && csv::field("plain") == "plain");

    // PSK Reporter datagram
    std::vector<uint8_t> b;
    auto spots = sampleSpots();
    size_t n = pskreport::build(b, spots, 0, true, "TA3BLD", "KM38NK", "HamOS", 1700000100, 7, 1);
    CHECK(n == 3 && b[0] == 0 && b[1] == 10 && ((b[2] << 8) | b[3]) == (int)b.size() && b.size() % 4 == 0);
    std::vector<pskreport::Spot> many(200, spots[0]);
    n = pskreport::build(b, many, 0, false, "TA3BLD", "KM38NK", "HamOS", 1, 0, 1);
    CHECK(n > 10 && n < 200 && b.size() <= pskreport::kMaxDatagram);

    // Cabrillo
    adif::Qso q;
    q.call = "DL1ABC"; q.freq = "14.0253"; q.mode = "CW"; q.date = "20261114"; q.time = "1203"; q.stx = "1"; q.srx = "23";
    CHECK(cabrillo::qsoLine(q, "TA3BLD") == "QSO: 14025 CW 2026-11-14 1203 TA3BLD        599 001    DL1ABC        599 023\n");
    q.freq = "145.500"; q.mode = "FM"; q.stx = q.srx = ""; q.rstSent = "59"; q.rstRcvd = "57";
    CHECK(cabrillo::qsoLine(q, "TA3BLD") == "QSO: 144   FM 2026-11-14 1203 TA3BLD        59         DL1ABC        57\n");
    cabrillo::Header h;
    h.contest = "TA-DX"; h.callsign = "TA3BLD";
    std::string head = cabrillo::header(h);
    CHECK(head.find("CATEGORY-POWER: LOW\n") != std::string::npos && head.find("CONTEST: TA-DX\n") != std::string::npos);

    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
