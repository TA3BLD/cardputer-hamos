#pragma once
#include "Arduino.h"
enum { HTTP_GET, HTTP_POST };
enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
struct HTTPUpload {
    int status = UPLOAD_FILE_START;
    uint8_t buf[16];
    size_t currentSize = 0;
};
struct WebServer {
    HTTPUpload up;
    explicit WebServer(int) {}
    void on(const char*, int, void (*)()) {}
    void on(const char*, int, void (*)(), void (*)()) {}
    void begin() {}
    void handleClient() {}
    HTTPUpload& upload() { return up; }
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
