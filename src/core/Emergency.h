// Emergency mode.
//
// SOS: after a 5 second countdown (any key cancels) a distress message with
// position, locator and battery goes out on every way available - Meshtastic,
// LoRa-APRS (emergency symbol), APRS-IS - and is repeated: every minute for
// five minutes, then every five minutes, until stopped. Stopping sends "OK".
// Status messages: short preset texts (OK, need help, ...) sent once.
// Safety timer: when no key is pressed for the set time, a loud warning; one
// minute later the SOS starts by itself.
// Siren (Morse SOS on the speaker), strobe (flashing screen), battery saver.
// Distress messages from others (SOS, MAYDAY, ACIL, ...) raise an alert in any app.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace emergency {

enum class State { Idle, Countdown, Active };

void begin();
void tick();
void keyPressed();                  // any key: resets the safety timer, cancels the countdown

// ---- SOS ----
void arm(const std::string& note);  // starts the 5 s countdown
void cancelCountdown();
void stop();                        // ends an active SOS and sends "OK"
State state();
int countdownSeconds();
int sentCount();
uint32_t nextSendInMs();
std::string lastText();
bool repeatedByMesh();              // another node repeated one of our SOS messages
std::string sentVia();              // "mesh, LoRa-APRS, APRS-IS" or why nothing could be sent

// ---- status messages ----
int presetCount();
const char* preset(int i);          // shown and sent text
bool sendPreset(int i, std::string& via);

// ---- replies and alerts ----
struct Alert {
    std::string from, text, via;
    uint32_t ms = 0;
    bool reply = false;             // arrived while our SOS was active (not a distress call)
};
const std::vector<Alert>& alerts();
bool takeNewDistress();             // a new distress message arrived since the last call

// ---- safety timer ----
void setTimer(int minutes);         // 0 = off
int timerMinutes();
int timerSecondsLeft();             // -1 when off
bool timerWarning();                // in the last minute

// ---- siren, strobe, saver ----
void setSiren(bool on);
bool siren();
void setStrobe(bool on);
bool strobe();
void setSaver(bool on);
bool saver();
bool ownsBacklight();               // strobe running: main must not change the brightness

}  // namespace emergency
