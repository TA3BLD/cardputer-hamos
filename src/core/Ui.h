// Shared drawing helpers so every app looks the same.
#pragma once
#include <string>
#include <vector>
#include "App.h"

namespace ui {

constexpr int W = 240, H = 135;
constexpr int STATUS_H = 14, FOOTER_H = 13;
constexpr int BODY_Y = STATUS_H, BODY_H = H - STATUS_H - FOOTER_H;
constexpr int ROW_H = 12;
constexpr int ROWS = BODY_H / ROW_H;

// RGB565
constexpr uint16_t C_BG = 0x0000, C_TEXT = 0xFFFF, C_DIM = 0x8410;
constexpr uint16_t C_BAR = 0x0A66, C_FOOT = 0x1084, C_SEL = 0x04DF;
constexpr uint16_t C_OK = 0x07E0, C_WARN = 0xFD20, C_ERR = 0xF800;

void begin();
void statusBar(const char* title);          // title + UTC + WiFi + battery
void footer(const char* hint);
void clearBody();
void fill(int x, int y, int w, int h, uint16_t color);
void tileLabel(int x, int yMiddle, const char* s, uint16_t bg);
void text(int x, int y, const std::string& s, uint16_t color = C_TEXT, float size = 1);
void textOn(int x, int y, const std::string& s, uint16_t color, uint16_t background);
// Replaces everything the built-in font cannot show: Turkish letters become
// their plain Latin form, other non-ASCII characters (emoji...) become '?'.
std::string ascii(const std::string& utf8);
void textCentered(int y, const std::string& s, uint16_t color = C_TEXT, float size = 1);
// Scrolling list; keeps `scroll` consistent with `selected`.
void list(const std::vector<std::string>& items, int selected, int& scroll,
          const std::vector<uint16_t>* colors = nullptr);
// Moves a list selection with the arrow keys. Returns true if it changed.
bool listNav(const KeyEvent& k, int& selected, int count);

// A labelled value on one line; the active one is highlighted and shows a cursor.
void formRow(int row, const char* label, const std::string& value, bool active, uint16_t valueColor = C_TEXT);
// Formats a number without trailing zeros ("14.2", "0.05", "inf").
std::string num(double v, int decimals = 2);

// Single-line text entry. Feed it key events while it is active.
struct TextInput {
    std::string value;
    size_t maxLen = 32;
    bool upper = false;                     // force upper case (callsigns)
    bool handle(const KeyEvent& k);         // true if value changed
    void draw(int y, const char* label) const;
};

}  // namespace ui
