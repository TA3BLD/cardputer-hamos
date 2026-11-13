#pragma once
#include "Arduino.h"
enum { HTTP_GET, HTTP_POST };
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
struct WebServer {
    explicit WebServer(int) {}
    void on(const char*, int, void (*)()) {}
    void begin() {}
    void handleClient() {}
    bool authenticate(const char*, const char*) { return true; }
    void requestAuthentication() {}
    String arg(const char*) { return ""; }
    bool hasArg(const char*) { return false; }
    void send(int, const char*, const char*) {}
    void sendHeader(const char*, const char*) {}
    void setContentLength(size_t) {}
    void sendContent(const char*) {}
    void sendContent(const char*, size_t) {}
};
