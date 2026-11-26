// Control from a computer over the USB cable (the Cardputer's USB serial port).
//
// Newline-separated JSON requests: {"id":1,"cmd":"status"} -> {"id":1,"ok":true,...}
// While the screen is mirrored, every drawing step is also sent as a line
// starting with '~' (see tools/pc/hamos.html for the reader). The PC program
// is tools/pc/hamos_pc.py, or tools/pc/hamos.html opened in Chrome / Edge.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "App.h"

namespace usb {

// What the link needs from the main program.
struct Host {
    void (*key)(const KeyEvent& k);             // as if typed on the keyboard
    void (*open)(App* app);                     // nullptr: back to the launcher
    App* (*current)();
    const std::vector<App*>* apps;
    void (*redraw)();                           // draw the whole screen again
};

void begin(const Host& host);
void tick();                                    // call from the main loop
bool connected();                               // a computer talked to us in the last few seconds

// Screen mirroring, called by the drawing functions in Ui.cpp.
bool mirroring();
void mirrorFill(int x, int y, int w, int h, uint16_t color);
void mirrorText(int x, int y, float size, uint16_t fg, int bg, const std::string& text);   // bg -1: transparent
void mirrorLine(int x0, int y0, int x1, int y1, uint16_t color);
void mirrorCircle(int x, int y, int r, uint16_t color, bool filled);
void mirrorTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
void mirrorFrame(int x, int y, int w, int h, uint16_t color);
void mirrorImage(int x, int y, int w, int h, const uint16_t* data);     // pixels high byte first

// Handles one request line and returns the reply line. `fromWeb`: it came
// over WiFi (the web page), so screen mirroring goes to webScreen().
std::string handle(const std::string& line, bool fromWeb = false);

// Drawing steps for the web page since its last call (it polls a few times a second).
std::string webScreen();

}  // namespace usb
