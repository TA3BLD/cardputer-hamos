// SD card access. HamOS still boots without a card; data just doesn't persist.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace storage {
bool begin();
bool ready();
bool readFile(const char* path, std::string& out);
bool writeFile(const char* path, const std::string& text);    // replaces the file
bool appendFile(const char* path, const std::string& text);   // creates it if missing
bool exists(const char* path);
bool remove(const char* path);
bool rename(const char* from, const char* to);
// Streams a file in pieces so large files never have to fit in RAM.
typedef void (*ChunkHandler)(const char* data, size_t len, void* user);
bool readChunks(const char* path, ChunkHandler handler, void* user);
struct Entry {
    std::string name;
    bool folder;
    uint32_t size;
};
// Everything in a folder, folders first, then by name.
std::vector<Entry> listDir(const char* folder);
bool makeDir(const char* path);
bool removeDir(const char* path);       // only an empty folder
// File names (no path) in a folder, sorted; folders are left out.
std::vector<std::string> listFiles(const char* folder);
// Reads `len` bytes at `offset`; returns the number read (0 on error).
size_t readAt(const char* path, uint32_t offset, void* buffer, size_t len);
uint32_t fileSize(const char* path);
}
