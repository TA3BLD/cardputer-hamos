#include "Config.h"
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
    std::string text;
    if (storage::readFile(kPath, text)) parse(text);
}

bool save() { return storage::writeFile(kPath, serialize()); }

std::string get(const std::string& key, const std::string& def) {
    auto it = values.find(key);
    return it == values.end() ? def : it->second;
}

void set(const std::string& key, const std::string& value) { values[key] = value; }
void erase(const std::string& key) { values.erase(key); }

}  // namespace config
