// Every HamOS application implements this interface. To add a new app:
// write a class deriving from App and register it in main.cpp.
#pragma once
#include <cstdint>

struct KeyEvent {
    char ch = 0;        // printable character, 0 for a pure control key
    bool enter = false;
    bool del = false;   // Backspace (or Fn+Backspace)
    bool tab = false;
    bool fn = false;
    bool esc = false;   // Fn + `
    bool arrowUp = false, arrowDown = false, arrowLeft = false, arrowRight = false;    // Fn + ; . , /

    // Navigation: the arrow keys (Fn + ; . , /) or, for convenience, the same
    // keys without Fn.
    bool up() const { return arrowUp || ch == ';'; }
    bool down() const { return arrowDown || ch == '.'; }
    bool left() const { return arrowLeft || ch == ','; }
    bool right() const { return arrowRight || ch == '/'; }
    // Only the real arrow combination, for screens where ; and . are text.
    // (Older versions of the keyboard library report Fn + ; as fn plus the character.)
    bool fnUp() const { return arrowUp || (fn && ch == ';'); }
    bool fnDown() const { return arrowDown || (fn && ch == '.'); }
    bool fnLeft() const { return arrowLeft || (fn && ch == ','); }
    bool fnRight() const { return arrowRight || (fn && ch == '/'); }
};

constexpr char KEY_BACK = '`';   // ` (or Fn+` = ESC): leave field / return to launcher

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
