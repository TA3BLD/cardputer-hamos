// The Meshtastic client API, so the official Meshtastic app (phone, PC) can
// use HamOS as its radio: it reads our node list, channel and messages and
// sends messages through our LoRa cap. Transport-free: the Bluetooth service
// (Ble.cpp) and the tests feed it ToRadio messages and take FromRadio messages.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace phoneapi {

void handleToRadio(const std::string& toRadio);
bool nextFromRadio(std::string& fromRadio);     // false when nothing is waiting
uint32_t fromNum();                             // counts messages queued, for the notify characteristic
void tick();                                    // forwards new mesh messages and nodes
void reset();                                   // the app disconnected
bool active();                                  // an app asked for the configuration since the last reset

}  // namespace phoneapi
