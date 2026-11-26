#pragma once
#include <algorithm>
#include <map>
#include <utility>
#include <vector>
#include "WiFi.h"
// Web pages: the body returned for any URL containing the key. A body
// "REDIRECT <url>" answers 302 to that URL; sim_http_status overrides the
// status code. Every request is recorded in sim_http_log.
#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_READ_TIMEOUT (-11)
inline std::map<std::string, std::string> sim_http;
inline std::map<std::string, int> sim_http_status;
struct SimHttpRequest {
    std::string method, url, body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string header(const std::string& name) const {
        for (auto& h : headers)
            if (h.first == name) return h.second;
        return "";
    }
};
inline std::vector<SimHttpRequest> sim_http_log;
enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };
struct HTTPClient {
    std::string url, location;
    SimHttpRequest req;
    const std::string* found = nullptr;
    void setTimeout(int) {}
    void setConnectTimeout(int) {}
    void setUserAgent(const String& ua) { req.headers.push_back({"User-Agent", ua.s}); }
    void setFollowRedirects(followRedirects_t) {}
    void collectHeaders(const char**, size_t) {}
    void useHTTP10(bool) {}
    bool begin(WiFiClient&, const char* u) { url = u; req = SimHttpRequest(); return true; }
    bool begin(WiFiClient& c, const String& u) { return begin(c, u.c_str()); }
    void addHeader(const String& k, const String& v) { req.headers.push_back({k.s, v.s}); }
    const std::string* body() {
        const std::string* best = nullptr;
        size_t bestLen = 0;
        for (auto& kv : sim_http)           // the longest matching key wins
            if (url.find(kv.first) != std::string::npos && kv.first.size() >= bestLen) { best = &kv.second; bestLen = kv.first.size(); }
        return best;
    }
    int sendRequest(const char* method, const String& payload = String()) {
        req.method = method;
        req.url = url;
        req.body = payload.s;
        sim_http_log.push_back(req);
        found = body();
        location.clear();
        if (!found) return -1;
        if (found->rfind("REDIRECT ", 0) == 0) {
            location = found->substr(9);
            return 302;
        }
        for (auto& kv : sim_http_status)
            if (url.find(kv.first) != std::string::npos) return kv.second;
        return 200;
    }
    int GET() { return sendRequest("GET"); }
    int POST(const String& b) { return sendRequest("POST", b); }
    String header(const char* name) { return std::string(name) == "Location" ? location : ""; }
    int getSize() { return found ? (int)found->size() : -1; }
    String getString() { return found ? *found : (body() ? *body() : std::string()); }
    int writeToStream(Stream* s) {
        if (!found) return -1;
        size_t done = 0;
        while (done < found->size()) {
            size_t n = std::min<size_t>(1024, found->size() - done);
            if (s->write((const uint8_t*)found->data() + done, n) != n) return -10;
            done += n;
        }
        return (int)done;
    }
    void end() {}
};
