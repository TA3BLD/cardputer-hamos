#include "Config.h"
#include <SD.h>
#include <map>
#include "Storage.h"

namespace config {

static std::map<std::string, std::string> values;
static const char* kPath = "/HamOS/config.txt";

void parse(const std::string& text) {
    values.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos || eq == 0) continue;
        values[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

std::string serialize() {
    std::string out = "# HamOS settings\n";
    for (const auto& kv : values) out += kv.first + "=" + kv.second + "\n";
    return out;
}

void load() {
    if (!storage::ready()) return;
    File f = SD.open(kPath);
    if (!f) return;
    std::string text;
    while (f.available()) text += (char)f.read();
    f.close();
    parse(text);
}

bool save() {
    if (!storage::ready()) return false;
    SD.remove(kPath);
    File f = SD.open(kPath, FILE_WRITE);
    if (!f) return false;
    f.print(serialize().c_str());
    f.close();
    return true;
}

std::string get(const std::string& key, const std::string& def) {
    auto it = values.find(key);
    return it == values.end() ? def : it->second;
}

void set(const std::string& key, const std::string& value) { values[key] = value; }

}  // namespace config
