// Firmware updates from the GitHub releases of the HamOS repository.
//
// Nothing is installed without asking: the check only reports a new version
// (status bar notice and the Update app), and the user decides with Y.
// The download is checked (size, SHA-256 when GitHub gives one, the ESP32's
// own image check) before the device switches to it, and the old version stays
// in the other app slot: if the new one fails to keep running for a minute on
// three starts in a row, bootGuard() switches back to it.
#pragma once
#include <string>
#include "lib/Release.h"

namespace updater {

enum class State { Idle, Checking, UpToDate, Available, Failed, Installing, Installed };

struct Status {
    State state = State::Idle;
    std::string message;        // error or progress text
    release::Info latest;       // when Available
    int percent = 0;            // while Installing
};

void bootGuard();               // first thing in setup()
void tick(bool idle);           // idle: nothing busy on screen, a check may block briefly
bool check();                   // asks GitHub now (a few seconds)
bool install();                 // after the user said yes; then restartNow()
void restartNow();
void onProgress(void (*hook)());    // called while installing when the percentage changes
const Status& status();
std::string rolledBackFrom();   // version that was undone at this start, "" normally
void afterManualUpdate();       // a firmware was installed another way (web page): watch it the same way

}  // namespace updater
