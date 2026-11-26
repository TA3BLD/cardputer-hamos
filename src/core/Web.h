// Optional web interface on the local WiFi network (off by default; switch it
// on with "Web UI" in Settings). Serves the same control panel as the PC
// program (tools/pc/hamos.html): live screen, keyboard, log, mesh, APRS,
// files and settings from a phone or PC browser.
// Set a "Web UI password" if other people share the network (user: hamos).
#pragma once
#include <string>

namespace web {
void tick();                // starts the server once WiFi is up; serves requests
bool running();
std::string address();      // "http://192.168.1.23", "" when not running
}
