// Bluetooth LE. Started only when something needs it (IC-705 link, Meshtastic
// phone app), because the Bluetooth stack takes a large share of the RAM.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace ble {

// ---- IC-705: CI-V over Bluetooth (HamOS is the client, the radio the server) ----
enum class Link { Off, Searching, Connecting, Pairing, Ready };
void ic705Start();                  // keeps looking for / reconnecting to the radio
void ic705Stop();
Link ic705State();
std::string ic705Status();          // a line for the screen
bool ic705Write(const uint8_t* data, size_t len);
size_t ic705Read(uint8_t* buf, size_t max);     // CI-V bytes from the radio

}  // namespace ble
