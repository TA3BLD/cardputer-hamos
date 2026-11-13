// HamOS - amateur radio toolkit for the M5Stack Cardputer / Cardputer ADV.
#include <M5Cardputer.h>
#include <vector>

#include "apps/ClockApp.h"
#include "apps/Launcher.h"
#include "apps/LocatorApp.h"
#include "apps/ReferenceApp.h"
#include "apps/SettingsApp.h"
#include "core/Config.h"
#include "core/Net.h"
#include "core/Storage.h"
#include "core/Ui.h"

// ---- App registry: add new apps here ---------------------------------------
static LocatorApp locatorApp;
static ClockApp clockApp;
static ReferenceApp referenceApp;
static SettingsApp settingsApp;
static const std::vector<App*> apps = {&locatorApp, &clockApp, &referenceApp, &settingsApp};
// -----------------------------------------------------------------------------

static Launcher launcher(apps);
static App* current = &launcher;
static uint32_t lastStatusMs = 0;

static void switchTo(App* next) {
    current->onExit();
    current = next;
    current->onEnter();
    current->dirty = true;
    lastStatusMs = 0;   // refresh the title now
}

static void dispatch(const KeyEvent& k) {
    if (k.ch == KEY_BACK) {
        if (!current->onBack() && current != &launcher) switchTo(&launcher);
        return;
    }
    current->onKey(k);
    if (current == &launcher && launcher.launch) {
        App* next = launcher.launch;
        launcher.launch = nullptr;
        switchTo(next);
    }
}

static void pollKeyboard() {
    if (!(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())) return;
    Keyboard_Class::KeysState state = M5Cardputer.Keyboard.keysState();

    bool sent = false;
    for (char c : state.word) {
        KeyEvent k;
        k.ch = c;
        k.fn = state.fn;
        dispatch(k);
        sent = true;
    }
    if (!sent && (state.enter || state.del || state.tab)) {
        KeyEvent k;
        k.enter = state.enter;
        k.del = state.del;
        k.tab = state.tab;
        k.fn = state.fn;
        dispatch(k);
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    ui::begin();
    ui::textCentered(50, "HamOS", ui::C_TEXT, 3);

    storage::begin();
    config::load();
    net::begin();

    std::string call = config::get("callsign");
    if (!call.empty()) ui::textCentered(85, "73 de " + call, ui::C_OK, 1.5);
    delay(800);
    current->onEnter();
}

void loop() {
    M5Cardputer.update();
    net::tick();
    pollKeyboard();

    uint32_t now = millis();
    current->onTick(now);

    if (lastStatusMs == 0 || now - lastStatusMs >= 1000) {
        lastStatusMs = now ? now : 1;
        ui::statusBar(current->name());
    }
    if (current->dirty) {
        current->dirty = false;
        current->draw();
    }
    delay(5);
}
