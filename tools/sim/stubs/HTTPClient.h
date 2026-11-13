#pragma once
#include "WiFi.h"
inline std::string sim_http_body;   // what the next GET returns
struct HTTPClient {
    void setTimeout(int) {}
    bool begin(WiFiClient&, const char*) { return true; }
    int GET() { return sim_http_body.empty() ? -1 : 200; }
    String getString() { return sim_http_body; }
    void end() {}
};
