#include "Ui.h"
#include <M5Cardputer.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include "AprsIs.h"
#include "Gps.h"
#include "Mesh.h"
#include "Net.h"

namespace ui {

static auto& lcd() { return M5Cardputer.Display; }

static std::string notice;
void setNotice(const std::string& text) { notice = text; }

void begin() {
    lcd().setRotation(1);
    lcd().setTextFont(1);
    lcd().fillScreen(C_BG);
}

void statusBar(const char* title) {
    lcd().fillRect(0, 0, W, STATUS_H, C_BAR);
    lcd().setTextSize(1);
    lcd().setTextColor(notice.empty() ? C_TEXT : C_NOTICE, C_BAR);
    lcd().setTextDatum(middle_left);
    lcd().drawString(notice.empty() ? title : notice.c_str(), 4, STATUS_H / 2);
    lcd().setTextColor(C_TEXT, C_BAR);

    char right[48];
    char clock[8] = "--:--";
    if (net::timeValid()) {
        time_t now = time(nullptr);
        struct tm t;
        gmtime_r(&now, &t);
        snprintf(clock, sizeof(clock), "%02d:%02d", t.tm_hour, t.tm_min);
    }
    char unread[12] = "";
    if (mesh::unread() > 0) snprintf(unread, sizeof(unread), " M%d", mesh::unread() > 99 ? 99 : mesh::unread());
    if (aprsis::unread() > 0) {
        size_t used = strlen(unread);
        snprintf(unread + used, sizeof(unread) - used, " A%d", aprsis::unread() > 9 ? 9 : aprsis::unread());
    }
    snprintf(right, sizeof(right), "%sz%s%s%s %d%%", clock, unread, gps::hasFix() ? " GPS" : "",
             net::connected() ? " WiFi" : "", (int)M5Cardputer.Power.getBatteryLevel());
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

void textOn(int x, int y, const std::string& s, uint16_t color, uint16_t background) {
    lcd().setTextSize(1);
    lcd().setTextColor(color, background);
    lcd().setTextDatum(top_left);
    lcd().drawString(s.c_str(), x, y);
}

std::string ascii(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            out += (c >= 32 && c < 127) ? (char)c : ' ';
            i++;
            continue;
        }
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        unsigned code = len == 2 && i + 1 < in.size() ? ((c & 0x1Fu) << 6) | ((unsigned char)in[i + 1] & 0x3Fu) : 0;
        switch (code) {
            case 0xE7: out += 'c'; break;   case 0xC7: out += 'C'; break;
            case 0x11F: out += 'g'; break;  case 0x11E: out += 'G'; break;
            case 0x131: out += 'i'; break;  case 0x130: out += 'I'; break;
            case 0xF6: out += 'o'; break;   case 0xD6: out += 'O'; break;
            case 0x15F: out += 's'; break;  case 0x15E: out += 'S'; break;
            case 0xFC: out += 'u'; break;   case 0xDC: out += 'U'; break;
            default: out += '?';
        }
        i += len;
        while (i < in.size() && ((unsigned char)in[i] & 0xC0) == 0x80 && len == 1) i++;   // stray continuation bytes
    }
    return out;
}

std::vector<std::string> wrapLines(const std::string& text, size_t width) {
    std::vector<std::string> out;
    std::string line;
    for (size_t i = 0; i <= text.size(); i++) {
        bool end = i == text.size(), newline = !end && text[i] == '\n';
        if (!end && !newline) line += text[i];
        if (line.size() > width) {                  // too long: break at the last space, else mid-word
            size_t space = line.rfind(' ', width);
            size_t cut = (space == std::string::npos || space == 0) ? width : space + 1;
            out.push_back(line.substr(0, cut));
            line.erase(0, cut);
        }
        if (newline || (end && !line.empty())) {
            out.push_back(line);
            line.clear();
        }
    }
    return out;
}

void textCentered(int y, const std::string& s, uint16_t color, float size) {
    lcd().setTextSize(size);
    lcd().setTextColor(color, C_BG);
    lcd().setTextDatum(top_center);
    lcd().drawString(s.c_str(), W / 2, y);
}

void list(const std::vector<std::string>& items, int selected, int& scroll, const std::vector<uint16_t>* colors, int top,
          int rows) {
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + rows) scroll = selected - rows + 1;
    if (scroll < 0) scroll = 0;
    lcd().fillRect(0, top, W, rows * ROW_H, C_BG);
    lcd().setTextSize(1);
    lcd().setTextDatum(middle_left);
    for (int row = 0; row < rows; row++) {
        int i = scroll + row;
        if (i >= (int)items.size()) break;
        int y = top + row * ROW_H;
        uint16_t bg = (i == selected) ? C_SEL : C_BG;
        if (i == selected) lcd().fillRect(0, y, W, ROW_H, bg);
        lcd().setTextColor(colors && i < (int)colors->size() ? (*colors)[i] : C_TEXT, bg);
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
