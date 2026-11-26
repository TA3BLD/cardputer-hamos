#include "Alarms.h"
#include <algorithm>
#include "Net.h"

namespace alarms {

namespace {
std::vector<Alarm> all;
}

void add(const std::string& text, long long t, int lead) {
    remove(text);
    all.push_back({text, t, lead, false});
}

void remove(const std::string& text) {
    all.erase(std::remove_if(all.begin(), all.end(), [&](const Alarm& a) { return a.text == text; }), all.end());
}

const std::vector<Alarm>& list() { return all; }

std::string tick() {
    if (!net::timeValid()) return "";
    long long now = net::utcMillis() / 1000;
    std::string due;
    for (Alarm& a : all)
        if (!a.fired && now >= a.unixTime - a.leadSeconds) {
            a.fired = true;
            due = a.text;
        }
    all.erase(std::remove_if(all.begin(), all.end(), [&](const Alarm& a) { return a.fired && now > a.unixTime + 600; }), all.end());
    return due;
}

}  // namespace alarms
