// Reminders at a set time (satellite passes, nets): a beep and a notice in the
// status bar `leadSeconds` before.
#pragma once
#include <string>
#include <vector>

namespace alarms {

struct Alarm {
    std::string text;
    long long unixTime = 0;
    int leadSeconds = 120;
    bool fired = false;
};

void add(const std::string& text, long long unixTime, int leadSeconds = 120);
void remove(const std::string& text);
const std::vector<Alarm>& list();
// Returns the text of an alarm that is due now ("" if none); main shows it.
std::string tick();

}  // namespace alarms
