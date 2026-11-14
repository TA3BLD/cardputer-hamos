// "SD card" backed by the ./simsd directory.
#pragma once
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include "Arduino.h"
#include "SPI.h"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

struct File {
    FILE* f = nullptr;
    DIR* d = nullptr;
    std::string path, entry;
    explicit operator bool() const { return f != nullptr || d != nullptr; }
    int available() { if (!f) return 0; int c = fgetc(f); if (c == EOF) return 0; ungetc(c, f); return 1; }
    int read(uint8_t* buf, size_t n) { return f ? (int)fread(buf, 1, n, f) : -1; }
    size_t write(const uint8_t* buf, size_t n) { return f ? fwrite(buf, 1, n, f) : 0; }
    bool seek(uint32_t pos) { return f && fseek(f, (long)pos, SEEK_SET) == 0; }
    size_t size() { if (!f) return 0; long at = ftell(f); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, at, SEEK_SET); return (size_t)n; }
    bool isDirectory() { return d != nullptr; }
    const char* name() { return entry.c_str(); }
    File openNextFile() {
        File next;
        if (!d) return next;
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string full = path + "/" + e->d_name;
            struct stat st;
            stat(full.c_str(), &st);
            next.entry = e->d_name;
            next.path = full;
            if (S_ISDIR(st.st_mode)) next.d = opendir(full.c_str());
            else next.f = fopen(full.c_str(), "r");
            return next;
        }
        return next;
    }
    void close() { if (f) fclose(f); if (d) closedir(d); f = nullptr; d = nullptr; }
};

struct SimSD {
    static std::string host(const char* p) { return std::string("simsd") + p; }
    bool begin(int, SimSPI&, int) { ::mkdir("simsd", 0755); return true; }
    bool exists(const char* p) { struct stat st; return stat(host(p).c_str(), &st) == 0; }
    bool mkdir(const char* p) { return ::mkdir(host(p).c_str(), 0755) == 0; }
    bool remove(const char* p) { return ::remove(host(p).c_str()) == 0; }
    bool rmdir(const char* p) { return ::rmdir(host(p).c_str()) == 0; }
    bool rename(const char* a, const char* b) { return ::rename(host(a).c_str(), host(b).c_str()) == 0; }
    File open(const char* p, const char* mode = "r") {
        File f;
        f.path = host(p);
        struct stat st;
        if (std::string(mode) == "r" && stat(f.path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) f.d = opendir(f.path.c_str());
        else f.f = fopen(f.path.c_str(), mode);
        return f;
    }
};
inline SimSD SD;
