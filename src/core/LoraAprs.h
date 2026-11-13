// LoRa-APRS on the cap's radio: the 70 cm amateur standard used by the
// LoRa trackers and iGates (433.775 MHz, SF12, 125 kHz, CR 4/5).
// The radio does either this or Meshtastic: "LoRa mode" in Settings.
// Packets are TNC2 text behind the 3 bytes "<\xff\x01".
#pragma once
#include <string>

namespace loraaprs {
void begin();               // (re)reads the settings; does nothing unless lora_mode is "aprs"
void tick();
bool enabled();
bool ready();
std::string status();
bool send(const std::string& tnc2);         // queued; false if not ready or the queue is full
float lastRssi();
float lastSnr();
unsigned heard();
}
