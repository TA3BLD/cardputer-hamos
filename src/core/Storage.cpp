#include "Storage.h"
#include <SD.h>
#include <SPI.h>

namespace storage {

// Cardputer / Cardputer ADV microSD wiring
static const int PIN_SCK = 40, PIN_MISO = 39, PIN_MOSI = 14, PIN_CS = 12;
static bool isReady = false;

bool begin() {
    // The LoRa cap shares this SPI bus: cap::begin() must already have parked its chip select.
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
    for (int attempt = 0; attempt < 3 && !isReady; attempt++) {
        isReady = SD.begin(PIN_CS, SPI, 25000000);
        if (!isReady) delay(200);
    }
    if (isReady && !SD.exists("/HamOS")) SD.mkdir("/HamOS");
    return isReady;
}

bool ready() { return isReady; }

bool exists(const char* path) { return isReady && SD.exists(path); }

bool remove(const char* path) { return isReady && SD.remove(path); }
bool rename(const char* from, const char* to) { return isReady && SD.rename(from, to); }

bool readChunks(const char* path, ChunkHandler handler, void* user) {
    if (!isReady) return false;
    File f = SD.open(path);
    if (!f) return false;
    uint8_t buf[512];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        handler((const char*)buf, (size_t)n, user);
    }
    f.close();
    return true;
}

bool readFile(const char* path, std::string& out) {
    out.clear();
    return readChunks(path, [](const char* d, size_t n, void* u) { ((std::string*)u)->append(d, n); }, &out);
}

static bool put(const char* path, const std::string& text, const char* mode) {
    if (!isReady) return false;
    File f = SD.open(path, mode);
    if (!f) return false;
    size_t written = f.write((const uint8_t*)text.data(), text.size());
    f.close();
    return written == text.size();
}

bool writeFile(const char* path, const std::string& text) {
    if (isReady) SD.remove(path);
    return put(path, text, FILE_WRITE);
}

bool appendFile(const char* path, const std::string& text) { return put(path, text, FILE_APPEND); }

}  // namespace storage
