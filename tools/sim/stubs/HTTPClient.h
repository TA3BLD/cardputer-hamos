#pragma once
#include <map>
#include "WiFi.h"
// Web pages: the body returned for any URL containing the key.
inline std::map<std::string, std::string> sim_http;
struct HTTPClient {
    std::string url;
    void setTimeout(int) {}
    bool begin(WiFiClient&, const char* u) { url = u; return true; }
    const std::string* body() {
        for (auto& kv : sim_http)
            if (url.find(kv.first) != std::string::npos) return &kv.second;
        return nullptr;
    }
    int GET() { return body() ? 200 : -1; }
    String getString() { return body() ? *body() : std::string(); }
    void end() {}
};
