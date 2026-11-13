#include "Ui.h"
#include <M5Cardputer.h>
#include <cmath>
#include <ctime>
#include "Net.h"

namespace ui {

static auto& lcd() { return M5Cardputer.Display; }

void begin() {
    lcd().setRotation(1);
    lcd().setTextFont(1);
    lcd().fillScreen(C_BG);
}

void statusBar(const char* title) {
    lcd().fillRect(0, 0, W, STATUS_H, C_BAR);
    lcd().setTextSize(1);
    lcd().setTextColor(C_TEXT, C_BAR);
    lcd().setTextDatum(middle_left);
    lcd().drawString(title, 4, STATUS_H / 2);

    char right[32];
    char clock[8] = "--:--";
    if (net::timeValid()) {
        time_t now = time(nullptr);
        struct tm t;
        gmtime_r(&now, &t);
        snprintf(clock, sizeof(clock), "%02d:%02d", t.tm_hour, t.tm_min);
    }
    snprintf(right, sizeof(right), "%sz %s %d%%", clock, net::connected() ? "WiFi" : "----",
             (int)M5Cardputer.Power.getBatteryLevel());
    lcd().setTextDatum(middle_right);
    lcd().drawString(right, W - 4, STATUS_H / 2);
}

void footer(const char* hint) {
    lcd().fillRect(0, H - FOOTER_H, W, FOOTER_H, C_FOOT);
    lcd().setTextSize(1);
    lcd().setTextColor(C_TEXT, C_FOOT);
    lcd().setTextDatum(middle_left);
    lcd().drawString(hint, 4, H - FOOTER_H / 2 - 1);
}

void fill(int x, int y, int w, int h, uint16_t color) { lcd().fillRect(x, y, w, h, color); }

void tileLabel(int x, int yMiddle, const char* s, uint16_t bg) {
    lcd().setTextSize(1.5);
    lcd().setTextColor(C_TEXT, bg);
    lcd().setTextDatum(middle_left);
    lcd().drawString(s, x, yMiddle);
}

void formRow(int row, const char* label, const std::string& value, bool active, uint16_t valueColor) {
    int y = BODY_Y + 2 + row * ROW_H;
    uint16_t bg = active ? C_FOOT : C_BG;
    lcd().fillRect(0, y, W, ROW_H, bg);
    lcd().setTextSize(1);
    lcd().setTextDatum(middle_left);
    lcd().setTextColor(active ? C_SEL : C_DIM, bg);
    lcd().drawString(label, 6, y + ROW_H / 2);
    lcd().setTextColor(valueColor, bg);
    lcd().drawString((value + (active ? "_" : "")).c_str(), 84, y + ROW_H / 2);
}

std::string num(double v, int decimals) {
    if (std::isnan(v)) return "-";
    if (std::isinf(v)) return "inf";
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

void clearBody() { lcd().fillRect(0, BODY_Y, W, BODY_H, C_BG); }

void text(int x, int y, const std::string& s, uint16_t color, float size) {
    lcd().setTextSize(size);
    lcd().setTextColor(color, C_BG);
    lcd().setTextDatum(top_left);
    lcd().drawString(s.c_str(), x, y);
}

void textCentered(int y, const std::string& s, uint16_t color, float size) {
    lcd().setTextSize(size);
    lcd().setTextColor(color, C_BG);
    lcd().setTextDatum(top_center);
    lcd().drawString(s.c_str(), W / 2, y);
}

void list(const std::vector<std::string>& items, int selected, int& scroll) {
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + ROWS) scroll = selected - ROWS + 1;
    if (scroll < 0) scroll = 0;
    clearBody();
    lcd().setTextSize(1);
    lcd().setTextDatum(middle_left);
    for (int row = 0; row < ROWS; row++) {
        int i = scroll + row;
        if (i >= (int)items.size()) break;
        int y = BODY_Y + row * ROW_H;
        uint16_t bg = (i == selected) ? C_SEL : C_BG;
        if (i == selected) lcd().fillRect(0, y, W, ROW_H, bg);
        lcd().setTextColor(C_TEXT, bg);
        lcd().drawString(items[i].c_str(), 6, y + ROW_H / 2);
    }
}

bool listNav(const KeyEvent& k, int& selected, int count) {
    if (count <= 0) return false;
    int before = selected;
    if (k.up()) selected = (selected + count - 1) % count;
    if (k.down()) selected = (selected + 1) % count;
    return selected != before;
}

bool TextInput::handle(const KeyEvent& k) {
    if (k.del) {
        if (value.empty()) return false;
        value.pop_back();
        return true;
    }
    if (k.ch >= 32 && k.ch < 127 && value.size() < maxLen) {
        value += upper ? (char)toupper((unsigned char)k.ch) : k.ch;
        return true;
    }
    return false;
}

void TextInput::draw(int y, const char* label) const {
    text(6, y, label, C_DIM);
    lcd().fillRect(6, y + 12, W - 12, 20, C_FOOT);
    lcd().setTextSize(1.5);
    lcd().setTextColor(C_TEXT, C_FOOT);
    lcd().setTextDatum(middle_left);
    // Show the tail if the value is wider than the box.
    const size_t fit = 24;
    std::string shown = value.size() > fit ? value.substr(value.size() - fit) : value;
    lcd().drawString((shown + "_").c_str(), 10, y + 22);
}

}  // namespace ui
