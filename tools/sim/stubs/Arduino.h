// Host stand-ins for the Arduino/M5 APIs HamOS uses. Simulator only.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>

#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define SERIAL_8N1 0

extern uint32_t sim_ms;
long long sim_utc_ms();         // the simulator's UTC clock (defined in sim.cpp)
inline uint32_t millis() { return sim_ms; }
inline void delay(int) {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void configTime(long, int, const char*, const char* = nullptr) {}
inline uint32_t esp_random() { return (uint32_t)rand() * 2654435761u + (uint32_t)rand(); }

struct String {
    std::string s;
    String() {}
    String(const char* c) : s(c) {}
    String(const std::string& c) : s(c) {}
    const char* c_str() const { return s.c_str(); }
};

struct SimEsp {
    uint64_t getEfuseMac() { return 0x78563412AABBULL; }    // node !12345678
    void restart() {}
};
inline SimEsp ESP;

// Byte stream the simulator script can fill (GNSS receiver, telnet server).
struct SimStream {
    std::deque<char> rx;
    void inject(const std::string& data) { rx.insert(rx.end(), data.begin(), data.end()); }
    int available() { return (int)rx.size(); }
    int read() { if (rx.empty()) return -1; char c = rx.front(); rx.pop_front(); return (unsigned char)c; }
};
struct SimSerial : SimStream { void begin(long, int, int, int) {} };
inline SimSerial Serial1;

// The USB serial port: the script (or devsim's pseudo terminal) writes into
// rx; what HamOS sends collects in tx, or goes to sim_usb_fd when set.
inline int sim_usb_fd = -1;
size_t sim_usb_write(const uint8_t* data, size_t n);       // defined in the simulator
struct SimUsbSerial : SimStream {
    std::string tx;
    void begin(unsigned long) {}
    size_t write(const uint8_t* data, size_t n) { return sim_usb_write(data, n); }
};
inline SimUsbSerial Serial;
