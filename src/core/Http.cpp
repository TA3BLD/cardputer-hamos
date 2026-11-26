#include "Http.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "Certs.h"
#include "Net.h"
#include "Storage.h"

namespace http {

namespace {

// GitHub's root certificates; a /HamOS/certs.pem on the SD card replaces them.
const char* roots() {
    static std::string fromCard;
    static bool looked = false;
    if (!looked) {
        looked = true;
        std::string pem;
        if (storage::readFile("/HamOS/certs.pem", pem) && pem.find("BEGIN CERTIFICATE") != std::string::npos) fromCard = pem;
    }
    return fromCard.empty() ? certs::kGithubRoots : fromCard.c_str();
}

std::string hostOf(const std::string& url) {
    size_t p = url.find("://");
    p = p == std::string::npos ? 0 : p + 3;
    size_t e = url.find_first_of("/:?", p);
    return url.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

// Adapts the body to a Sink (HTTPClient::writeToStream wants a Stream and
// takes care of chunked replies).
class SinkStream : public Stream {
public:
    SinkStream(Sink s, void* c) : sink_(s), ctx_(c) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* data, size_t n) override {
        if (stopped_ || !sink_(data, n, ctx_)) {
            stopped_ = true;
            return 0;
        }
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    bool stopped() const { return stopped_; }

private:
    Sink sink_;
    void* ctx_;
    bool stopped_ = false;
};

Response run(const Request& r, Sink sink, void* ctx, void (*onSize)(long, void*)) {
    Response out;
    if (!net::connected()) {
        out.error = "No WiFi";
        return out;
    }
    std::string url = r.url, method = r.method, body = r.body;
    std::vector<Header> headers = r.headers;
    std::string firstHost = hostOf(url);
    for (int hop = 0; hop < 6; hop++) {
        bool tls = url.rfind("https://", 0) == 0;
        WiFiClientSecure secure;
        WiFiClient plain;
        if (tls) {
            if (r.verify) secure.setCACert(roots());
            else secure.setInsecure();
        }
        HTTPClient h;
        h.setTimeout((int)r.timeoutMs);
        h.setConnectTimeout((int)r.timeoutMs);
        h.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        static const char* collect[] = {"Location"};
        h.collectHeaders(collect, 1);
        bool begun = tls ? h.begin(secure, url.c_str()) : h.begin(plain, url.c_str());
        if (!begun) {
            out.error = "Bad address";
            return out;
        }
        h.setUserAgent("HamOS");
        if (!r.contentType.empty() && !body.empty()) h.addHeader("Content-Type", r.contentType.c_str());
        bool sameHost = hostOf(url) == firstHost;
        for (const Header& hd : headers)
            if (sameHost || hd.name != "Authorization") h.addHeader(hd.name.c_str(), hd.value.c_str());
        int code = h.sendRequest(method.c_str(), String(body.c_str()));
        if (code <= 0) {
            out.error = tls && r.verify ? "Secure connection failed" : "Server not reachable";
            if (code == HTTPC_ERROR_CONNECTION_REFUSED && tls) out.error = "Secure connection failed (memory?)";
            else if (code == HTTPC_ERROR_READ_TIMEOUT) out.error = "No answer (timeout)";
            h.end();
            return out;
        }
        out.status = code;
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            std::string loc = h.header("Location").c_str();
            h.end();
            if (loc.empty()) {
                out.error = "Redirect without address";
                return out;
            }
            if (loc[0] == '/') {
                size_t p = url.find("://");
                size_t e = url.find('/', p == std::string::npos ? 0 : p + 3);
                loc = url.substr(0, e) + loc;
            }
            url = loc;
            if (code == 303 || ((code == 301 || code == 302) && method == "POST")) {
                method = "GET";
                body.clear();
            }
            continue;
        }
        long size = h.getSize();
        if (onSize) onSize(size, ctx);
        SinkStream s(sink, ctx);
        int n = h.writeToStream(&s);
        h.end();
        if (n < 0 && !s.stopped()) out.error = "Download interrupted";
        if (out.error.empty() && (code < 200 || code >= 300)) out.error = "Server answered " + std::to_string(code);
        return out;
    }
    out.error = "Too many redirects";
    return out;
}

struct Collect {
    std::string* body;
    size_t max;
    bool tooLong = false;
};

}  // namespace

Response fetch(const Request& r) {
    Response out;
    Collect c{&out.body, r.maxBytes};
    Response res = run(
            r,
            [](const uint8_t* d, size_t n, void* p) {
                Collect* c = (Collect*)p;
                if (c->body->size() + n > c->max) {
                    c->tooLong = true;
                    return false;
                }
                c->body->append((const char*)d, n);
                return true;
            },
            &c, nullptr);
    res.body.swap(out.body);
    if (c.tooLong && res.error.empty()) res.error = "Answer too long";
    return res;
}

Response stream(const Request& r, Sink sink, void* ctx, void (*onSize)(long, void*)) { return run(r, sink, ctx, onSize); }

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

}  // namespace http
