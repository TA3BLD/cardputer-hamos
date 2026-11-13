// Country flags from /HamOS/flags on the SD card (made by tools/flags/make_flags.py).
#pragma once

namespace flags {
constexpr int SMALL_W = 16, SMALL_H = 11, LARGE_W = 48, LARGE_H = 32;
bool drawSmall(const char* iso, int x, int y);     // false if there is no such flag
bool drawLarge(const char* iso, int x, int y);
}
