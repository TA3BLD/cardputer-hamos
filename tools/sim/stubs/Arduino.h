// Host stand-ins for the Arduino/M5 APIs HamOS uses. Simulator only.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
extern uint32_t sim_ms;
inline uint32_t millis() { return sim_ms; }
inline void delay(int) {}
inline void configTime(long, int, const char*, const char* = nullptr) {}
