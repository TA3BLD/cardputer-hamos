// Host test for the web API helpers: JSON tree reader and GitHub release parsing.
//   g++ -std=c++17 -I src test/host/test_web.cpp -o test/host/t.out && test/host/t.out
#include <cassert>
#include <cstdio>
#include "lib/JsonDom.h"
#include "lib/Release.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main() {
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

    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
