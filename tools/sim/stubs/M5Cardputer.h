#pragma once
#include <vector>
#include "Arduino.h"

enum textdatum_t { top_left, top_center, middle_left, middle_right, middle_center };

// Records drawing commands on stdout; render.py turns them into images.
struct SimDisplay {
    float size = 1;
    int fg = 0xFFFF, bg = 0, datum = top_left;
    void setRotation(int) {}
    void setTextFont(int) {}
    void fillScreen(int c) { fillRect(0, 0, 240, 135, c); }
    void fillRect(int x, int y, int w, int h, int c) { printf("R %d %d %d %d %d\n", x, y, w, h, c); }
    void setTextSize(float s) { size = s; }
    void setTextColor(int f, int b) { fg = f; bg = b; }
    void setTextDatum(textdatum_t d) { datum = d; }
    void drawString(const char* s, int x, int y) { printf("T %d %d %g %d %d %d %s\n", x, y, size, datum, fg, bg, s); }
};

struct Keyboard_Class {
    struct KeysState {
        std::vector<char> word;
        bool enter = false, del = false, tab = false, fn = false;
    };
    KeysState state;
    bool changed = false;
    void inject(const KeysState& s) { state = s; changed = true; }
    bool isChange() { bool c = changed; changed = false; return c; }
    bool isPressed() { return true; }
    KeysState& keysState() { return state; }
};

struct SimPower { int getBatteryLevel() { return 87; } };
struct SimSpeaker { void tone(float, uint32_t) {} };
struct SimConfig {};
struct SimI2C {
    bool scanID(uint8_t, uint32_t) { return true; }
    uint8_t readRegister8(uint8_t, uint8_t, uint32_t) { return 0; }
    bool writeRegister8(uint8_t, uint8_t, uint8_t, uint32_t) { return true; }
};
struct SimM5 { SimI2C In_I2C; SimConfig config() { return {}; } };
inline SimM5 M5;
struct SimCardputer {
    SimDisplay Display;
    Keyboard_Class Keyboard;
    SimPower Power;
    SimSpeaker Speaker;
    void begin(SimConfig, bool) {}
    void update() {}
};
inline SimCardputer M5Cardputer;
