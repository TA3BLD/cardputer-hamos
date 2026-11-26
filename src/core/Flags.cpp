#include "Flags.h"
#include <cstring>
#include <string>
#include "Storage.h"
#include "Ui.h"

namespace flags {

namespace {

// The small flags of the last few countries drawn stay in memory: lists
// redraw often and SD reads are slow.
struct Cached {
    char iso[8];
    bool present;
    uint16_t pixels[SMALL_W * SMALL_H];
    uint32_t used;
};
Cached cache[20];
uint32_t useCounter = 0;

bool load(const char* folder, const char* iso, int w, int h, uint16_t* pixels) {
    std::string path = std::string("/HamOS/flags/") + folder + "/" + iso + ".bin";
    uint8_t head[4];
    if (storage::readAt(path.c_str(), 0, head, 4) != 4 || head[0] != 'F' || head[1] != 'L' || head[2] != w || head[3] != h)
        return false;
    size_t bytes = (size_t)w * h * 2;
    return storage::readAt(path.c_str(), 4, pixels, bytes) == bytes;
}

}  // namespace

bool drawSmall(const char* iso, int x, int y) {
    if (!iso || !*iso || strlen(iso) >= sizeof(cache[0].iso)) return false;
    Cached* hit = nullptr;
    for (Cached& c : cache)
        if (c.used && strcmp(c.iso, iso) == 0) hit = &c;
    if (!hit) {
        hit = &cache[0];
        for (Cached& c : cache)
            if (c.used < hit->used) hit = &c;           // the least recently used (or empty) slot
        strcpy(hit->iso, iso);
        hit->present = load("s", iso, SMALL_W, SMALL_H, hit->pixels);
    }
    hit->used = ++useCounter;
    if (hit->present) ui::image(x, y, SMALL_W, SMALL_H, hit->pixels);
    return hit->present;
}

bool drawLarge(const char* iso, int x, int y) {
    static uint16_t pixels[LARGE_W * LARGE_H];
    if (!iso || !*iso || !load("l", iso, LARGE_W, LARGE_H, pixels)) return false;
    ui::image(x, y, LARGE_W, LARGE_H, pixels);
    return true;
}

}  // namespace flags
