// Host-side test: g++ -std=c++17 -I src test/host/test_json.cpp -o test/host/t.out && test/host/t.out
#include <cstdio>
#include "lib/Base64.h"
#include "lib/MiniJson.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main() {
    json::Object o;
    CHECK(json::parse("{\"id\": 7, \"cmd\":\"mesh.send\", \"text\":\"G\\u00fcnayd\\u0131n \\\"73\\\"\\n\", \"on\":true, \"x\":-1.5e2, \"n\":null}", o));
    CHECK(o.num("id") == 7 && o.str("cmd") == "mesh.send" && o.flag("on") && o.num("x") == -150 && o.str("n") == "null");
    CHECK(o.str("text") == "G\xc3\xbcnayd\xc4\xb1n \"73\"\n");
    CHECK(json::parse("{\"e\":\"\\ud83d\\ude00\"}", o) && o.str("e") == "\xf0\x9f\x98\x80");
    CHECK(json::parse("{}", o) && o.values.empty());
    for (const char* bad : {"", "[]", "{\"a\":}", "{\"a\":1", "{\"a\" 1}", "{\"a\":{\"b\":1}}", "{\"a\":\"x}", "not json"})
        CHECK(!json::parse(bad, o));

    json::Writer w;
    w.obj().kv("id", 3).kv("ok", true).kv("name", "a\"b\\c\n\x01").key("list").arr().val(1).val("x").obj().kv("k", 2.5).end().end()
        .key("empty").arr().end().key("o").obj().end().end();
    CHECK(w.out == "{\"id\":3,\"ok\":true,\"name\":\"a\\\"b\\\\c\\n\\u0001\",\"list\":[1,\"x\",{\"k\":2.5}],\"empty\":[],\"o\":{}}");
    json::Writer t;
    t.arr().val("x").val(1.0 / 0.0).end();
    CHECK(t.out == "[\"x\",null]");

    for (std::string s : {"", "f", "fo", "foo", "foob", "fooba", "foobar"}) {
        std::string e = base64::encode((const uint8_t*)s.data(), s.size());
        CHECK(base64::decode(e) == s);
    }
    CHECK(base64::encode((const uint8_t*)"foobar", 6) == "Zm9vYmFy" && base64::encode((const uint8_t*)"fo", 2) == "Zm8=");
    std::string bin;
    for (int i = 0; i < 256; i++) bin += (char)i;
    CHECK(base64::decode(base64::encode((const uint8_t*)bin.data(), bin.size())) == bin);
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
