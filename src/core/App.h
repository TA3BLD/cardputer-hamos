// Every HamOS application implements this interface. To add a new app:
// write a class deriving from App and register it in main.cpp.
#pragma once
#include <cstdint>

struct KeyEvent {
    char ch = 0;        // printable character, 0 for a pure control key
    bool enter = false;
    bool del = false;
    bool tab = false;
    bool fn = false;

    // Cardputer arrow cluster (the keys with arrows printed on them).
    bool up() const { return ch == ';'; }
    bool down() const { return ch == '.'; }
    bool left() const { return ch == ','; }
    bool right() const { return ch == '/'; }
};

constexpr char KEY_BACK = '`';   // ESC key: leave field / return to launcher

class App {
public:
    virtual ~App() {}
    virtual const char* name() const = 0;
    virtual uint16_t color() const { return 0x04DF; }   // launcher tile accent (RGB565)
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onKey(const KeyEvent&) {}
    virtual void onTick(uint32_t /*nowMs*/) {}
    // Return true if the app consumed the back key (e.g. to cancel an edit);
    // false sends the user back to the launcher.
    virtual bool onBack() { return false; }
    // Draw the body area only; the status bar belongs to the OS.
    virtual void draw() = 0;

    bool dirty = true;   // set to request a redraw
};
