#pragma once
#include <vector>
#include "Arduino.h"

enum textdatum_t { top_left, top_center, middle_left, middle_right, middle_center };

// Records drawing commands on stdout; render.py turns them into images.
struct SimDisplay {
    float size = 1;
    int fg = 0xFFFF, bg = 0, datum = top_left;
    void setRotation(int) {}
    void setBrightness(uint8_t) {}
    void setTextFont(int) {}
    void fillScreen(int c) { fillRect(0, 0, 240, 135, c); }
    void fillRect(int x, int y, int w, int h, int c) { printf("R %d %d %d %d %d\n", x, y, w, h, c); }
    void setTextSize(float s) { size = s; }
    void setTextColor(int f, int b) { fg = f; bg = b; }
    void setTextDatum(textdatum_t d) { datum = d; }
    void drawString(const char* s, int x, int y) { printf("T %d %d %g %d %d %d %s\n", x, y, size, datum, fg, bg, s); }
};

// Keyboard: the script queues key states; each isChange() call moves on to the
// next one, like keys going down and up. Field layout as in M5Cardputer 1.1.1.
struct Keyboard_Class {
    struct KeysState {
        std::vector<char> word;
        bool tab = false, fn = false, shift = false, ctrl = false, opt = false, alt = false;
        bool backspace = false, del = false, enter = false, space = false, esc = false;
        bool up = false, left = false, down = false, right = false;
        uint8_t count() const {
            return (uint8_t)(word.size() + tab + fn + shift + ctrl + opt + alt + backspace + del + enter + esc + up + left + down + right);
        }
    };
    std::deque<KeysState> script;
    KeysState state;
    bool isChange() {
        if (script.empty()) return false;
        state = script.front();
        script.pop_front();
        return true;
    }
    uint8_t isPressed() { return state.count(); }
    KeysState& keysState() { return state; }
};

struct SimPower { int getBatteryLevel() { return 87; } };
// Speaker: everything "played" is kept so the script can check it.
inline std::vector<int16_t> sim_speaker;
struct SimSpeaker {
    void tone(float, uint32_t) {}
    bool begin() { return true; }
    void end() {}
    void stop() {}
    size_t isPlaying(uint8_t) { return 0; }
    bool playRaw(const int16_t* data, size_t n, uint32_t, bool, uint32_t, int) {
        sim_speaker.insert(sim_speaker.end(), data, data + n);
        return true;
    }
};

// Microphone: plays back whatever the script queued, silence otherwise.
inline std::deque<int16_t> sim_mic;
struct SimMicConfig { uint32_t sample_rate = 16000; };
struct SimMic {
    SimMicConfig cfg;
    SimMicConfig config() { return cfg; }
    void config(const SimMicConfig& c) { cfg = c; }
    bool begin() { return true; }
    void end() {}
    bool record(int16_t* out, size_t n, uint32_t) {
        for (size_t i = 0; i < n; i++) {
            out[i] = sim_mic.empty() ? 0 : sim_mic.front();
            if (!sim_mic.empty()) sim_mic.pop_front();
        }
        return true;
    }
};
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
    SimMic Mic;
    void begin(SimConfig, bool) {}
    void update() {}
};
inline SimCardputer M5Cardputer;
