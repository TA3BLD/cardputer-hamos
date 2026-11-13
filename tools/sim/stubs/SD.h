// "SD card" backed by the ./simsd directory.
#pragma once
#include <sys/stat.h>
#include "Arduino.h"
#include "SPI.h"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

struct File {
    FILE* f = nullptr;
    explicit operator bool() const { return f != nullptr; }
    int available() { int c = fgetc(f); if (c == EOF) return 0; ungetc(c, f); return 1; }
    int read(uint8_t* buf, size_t n) { return (int)fread(buf, 1, n, f); }
    size_t write(const uint8_t* buf, size_t n) { return fwrite(buf, 1, n, f); }
    void close() { if (f) fclose(f); f = nullptr; }
};

struct SimSD {
    static std::string host(const char* p) { return std::string("simsd") + p; }
    bool begin(int, SimSPI&, int) { ::mkdir("simsd", 0755); return true; }
    bool exists(const char* p) { struct stat st; return stat(host(p).c_str(), &st) == 0; }
    bool mkdir(const char* p) { return ::mkdir(host(p).c_str(), 0755) == 0; }
    bool remove(const char* p) { return ::remove(host(p).c_str()) == 0; }
    bool rename(const char* a, const char* b) { return ::rename(host(a).c_str(), host(b).c_str()) == 0; }
    File open(const char* p, const char* mode = "r") { File f; f.f = fopen(host(p).c_str(), mode); return f; }
};
inline SimSD SD;
