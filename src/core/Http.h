// HTTP and HTTPS requests for apps (blocking; a few seconds at most).
//
//   http::Request r;
//   r.url = "https://api.pota.app/spot/activator";
//   http::Response a = http::fetch(r);
//   if (a.ok()) ... a.body ...
//
// Redirects are followed; an Authorization header is not passed on to another
// host. Public data uses unchecked TLS like the rest of HamOS; set `verify`
// for anything that must really come from GitHub (firmware updates).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace http {

struct Header {
    std::string name, value;
};

struct Request {
    std::string method = "GET";
    std::string url;
    std::string body;
    std::string contentType;            // for a body: "application/json", "application/x-www-form-urlencoded"
    std::vector<Header> headers;
    bool verify = false;                // check the server certificate against GitHub's roots (Certs.h)
    size_t maxBytes = 48 * 1024;        // fetch(): longer bodies are an error
    uint32_t timeoutMs = 10000;
};

struct Response {
    int status = 0;                     // HTTP status, or 0 when the request did not get that far
    std::string body;
    std::string error;                  // set when !ok()
    bool ok() const { return status >= 200 && status < 300 && error.empty(); }
};

// Receives the body in pieces; return false to stop.
using Sink = bool (*)(const uint8_t* data, size_t len, void* ctx);

Response fetch(const Request& r);
// Streams the body to `sink` instead of keeping it. `onSize` (optional) gets
// the length before the first piece, -1 when the server does not say.
Response stream(const Request& r, Sink sink, void* ctx, void (*onSize)(long size, void* ctx) = nullptr);

std::string urlEncode(const std::string& s);

}  // namespace http
