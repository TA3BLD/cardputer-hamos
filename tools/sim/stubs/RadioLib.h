// Fake SX1262: packets the script injects are "received"; transmissions
// complete immediately and are kept in sim_lora_sent.
#pragma once
#include <vector>
#include "Arduino.h"
#include "SPI.h"
#define RADIOLIB_ERR_NONE 0

inline std::deque<std::string> sim_lora_rx;
inline std::vector<std::string> sim_lora_sent;
inline void (*sim_lora_isr)() = nullptr;
inline void sim_lora_inject(const std::string& packet) {
    sim_lora_rx.push_back(packet);
    if (sim_lora_isr) sim_lora_isr();
}

struct Module { Module(int, int, int, int, SimSPI&) {} };
struct SX1262 {
    explicit SX1262(Module*) {}
    int16_t begin(float, float, uint8_t, uint8_t, uint8_t, int8_t, uint16_t, float, bool) { return 0; }
    void setDio2AsRfSwitch(bool) {}
    void setCurrentLimit(float) {}
    void setRxBoostedGainMode(bool) {}
    void setCRC(uint8_t) {}
    void setDio1Action(void (*f)()) { sim_lora_isr = f; }
    int16_t startReceive() { return 0; }
    size_t getPacketLength() { return sim_lora_rx.empty() ? 0 : sim_lora_rx.front().size(); }
    int16_t readData(uint8_t* buf, size_t len) {
        memcpy(buf, sim_lora_rx.front().data(), len);
        sim_lora_rx.pop_front();
        return 0;
    }
    float getRSSI() { return -97.0f; }
    float getSNR() { return 6.25f; }
    int16_t startTransmit(const uint8_t* data, size_t len) {
        sim_lora_sent.push_back(std::string((const char*)data, len));
        if (sim_lora_isr) sim_lora_isr();
        return 0;
    }
    int16_t finishTransmit() { return 0; }
};
