// Optional web interface on the local WiFi network (off by default; switch it
// on with "Web UI" in Settings). Download the log, read and send mesh
// messages and edit settings from a phone or PC browser.
// Set a "Web UI password" if other people share the network (user: hamos).
#pragma once
#include <string>

namespace web {
void tick();                // starts the server once WiFi is up; serves requests
bool running();
std::string address();      // "http://192.168.1.23", "" when not running
}
