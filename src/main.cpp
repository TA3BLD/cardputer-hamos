// HamOS - amateur radio toolkit for the M5Stack Cardputer / Cardputer ADV.
#include <M5Cardputer.h>
#include <vector>

#include "apps/AprsApp.h"
#include "apps/CalcApp.h"
#include "apps/ClockApp.h"
#include "apps/CountryApp.h"
#include "apps/DigiApp.h"
#include "apps/DxClusterApp.h"
#include "apps/Ft8App.h"
#include "apps/WsjtxApp.h"
#include "apps/GpsApp.h"
#include "apps/GuideApp.h"
#include "apps/Launcher.h"
#include "apps/LocatorApp.h"
#include "apps/LogbookApp.h"
#include "apps/MapApp.h"
#include "apps/MeshApp.h"
#include "apps/MorseApp.h"
#include "apps/PskApp.h"
#include "apps/ReferenceApp.h"
#include "apps/RepeaterApp.h"
#include "apps/SatApp.h"
#include "apps/SettingsApp.h"
#include "apps/SolarApp.h"
#include "apps/SstvApp.h"
#include "core/AprsIs.h"
#include "core/Audio.h"
#include "core/Cap.h"
#include "core/Config.h"
#include "core/Gps.h"
#include "core/LoraAprs.h"
#include "core/Mesh.h"
#include "core/Net.h"
#include "core/Rig.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "core/Web.h"
#include "core/Wsjtx.h"

// ---- App registry: add new apps here ---------------------------------------
static LogbookApp logbookApp;
static MeshApp meshApp;
static AprsApp aprsApp;
static MapApp mapApp;
static DxClusterApp dxClusterApp;
static Ft8App ft8App;
static WsjtxApp wsjtxApp;
static SolarApp solarApp;
static SatApp satApp;
static GpsApp gpsApp;
static DigiApp digiApp;
static SstvApp sstvApp;
static PskApp pskApp;
static LocatorApp locatorApp;
static RepeaterApp repeaterApp;
static CalcApp calcApp;
static MorseApp morseApp;
static ClockApp clockApp;
static ReferenceApp referenceApp;
static GuideApp guideApp;
static CountryApp countryApp;
static SettingsApp settingsApp;
static const std::vector<App*> apps = {&logbookApp, &meshApp,     &aprsApp,    &mapApp,     &ft8App,     &dxClusterApp,
                                       &wsjtxApp,   &solarApp,    &satApp,     &gpsApp,     &digiApp,    &sstvApp,
                                       &pskApp,     &locatorApp,  &repeaterApp, &calcApp,   &morseApp,   &clockApp,
                                       &referenceApp, &guideApp,  &countryApp, &settingsApp};
// -----------------------------------------------------------------------------

static Launcher launcher(apps);
static App* current = &launcher;
static uint32_t lastStatusMs = 0;

// Screen dimming after a period without key presses ("dim_after" seconds, 0 = never).
static uint32_t lastKeyMs = 0;
static bool dimmed = false;

static uint8_t brightness() {
    int b = atoi(config::get("brightness", "160").c_str());
    return (uint8_t)(b < 10 ? 10 : b > 255 ? 255 : b);
}

static void noteActivity() {
    lastKeyMs = millis();
    if (dimmed) {
        M5Cardputer.Display.setBrightness(brightness());
        dimmed = false;
    }
}

static void switchTo(App* next) {
    current->onExit();
    current = next;
    current->onEnter();
    current->dirty = true;
    lastStatusMs = 0;   // refresh the title now
}

static void dispatch(const KeyEvent& k) {
    if (k.ch == KEY_BACK || k.esc) {
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

// M5Cardputer 1.1.1 reports Backspace, Esc and the Fn+arrow keys in separate
// fields that older versions of the library do not have (there Backspace is
// "del"). These helpers read a field if it exists and return false otherwise,
// so HamOS builds and behaves the same with both.
#define HAMOS_KEY_FIELD(name)                                                                              \
    template <class T> static auto keyField_##name(const T& s, int) -> decltype((bool)s.name) { return s.name; } \
    template <class T> static bool keyField_##name(const T&, long) { return false; }
HAMOS_KEY_FIELD(backspace)
HAMOS_KEY_FIELD(esc)
HAMOS_KEY_FIELD(up)
HAMOS_KEY_FIELD(down)
HAMOS_KEY_FIELD(left)
HAMOS_KEY_FIELD(right)

static void pollKeyboard() {
    // What was held at the last change, so that a key is reported once: when it
    // goes down, not again while a second key is pressed or released next to it.
    static std::vector<char> heldChars;
    static KeyEvent heldKeys;
    static uint8_t heldCount = 0;

    if (!M5Cardputer.Keyboard.isChange()) return;
    uint8_t count = M5Cardputer.Keyboard.isPressed();
    if (count == 0) {
        heldChars.clear();
        heldKeys = KeyEvent();
        heldCount = 0;
        return;
    }
    Keyboard_Class::KeysState& state = M5Cardputer.Keyboard.keysState();
    noteActivity();

    KeyEvent now;
    now.fn = state.fn;
    now.enter = state.enter;
    now.tab = state.tab;
    now.del = state.del || keyField_backspace(state, 0);
    now.esc = keyField_esc(state, 0);
    now.arrowUp = keyField_up(state, 0);
    now.arrowDown = keyField_down(state, 0);
    now.arrowLeft = keyField_left(state, 0);
    now.arrowRight = keyField_right(state, 0);

    if (count > heldCount) {        // something was pressed (a release changes nothing)
        KeyEvent k;
        k.fn = now.fn;
        k.enter = now.enter && !heldKeys.enter;
        k.tab = now.tab && !heldKeys.tab;
        k.del = now.del && !heldKeys.del;
        k.esc = now.esc && !heldKeys.esc;
        k.arrowUp = now.arrowUp && !heldKeys.arrowUp;
        k.arrowDown = now.arrowDown && !heldKeys.arrowDown;
        k.arrowLeft = now.arrowLeft && !heldKeys.arrowLeft;
        k.arrowRight = now.arrowRight && !heldKeys.arrowRight;
        if (k.enter || k.tab || k.del || k.esc || k.arrowUp || k.arrowDown || k.arrowLeft || k.arrowRight) dispatch(k);
        for (char c : state.word) {
            bool held = false;
            for (char h : heldChars) held |= (h == c);
            if (held) continue;
            KeyEvent e;
            e.ch = c;
            e.fn = now.fn;
            dispatch(e);
        }
    }
    heldChars.assign(state.word.begin(), state.word.end());
    heldKeys = now;
    heldCount = count;
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    cap::begin();           // before the SD card: parks the radio on the shared SPI bus
    ui::begin();
    ui::textCentered(50, "HamOS", ui::C_TEXT, 3);

    storage::begin();
    config::load();
    net::begin();
    gps::begin();
    mesh::begin();
    loraaprs::begin();
    aprsis::begin();
    rig::begin();
    wsjtxlink::begin();
    M5Cardputer.Display.setBrightness(brightness());
    noteActivity();

    std::string call = config::get("callsign");
    if (!call.empty()) ui::textCentered(85, "73 de " + call, ui::C_OK, 1.5);
    delay(800);
    current->onEnter();
}

void loop() {
    M5Cardputer.update();
    net::tick();
    gps::tick();
    mesh::tick();
    loraaprs::tick();
    web::tick();
    aprsis::tick();
    rig::tick();
    wsjtxlink::tick();
    audio::tick();
    pollKeyboard();

    uint32_t now = millis();
    current->onTick(now);

    // Station identification reminder ("id_timer" minutes, 0 = off).
    static uint32_t lastIdMs = 0, idNoticeMs = 0;
    int idMinutes = atoi(config::get("id_timer", "0").c_str());
    if (idMinutes > 0 && now - lastIdMs > (uint32_t)idMinutes * 60000) {
        lastIdMs = now;
        idNoticeMs = now ? now : 1;
        ui::setNotice("ID: give your callsign");
        M5Cardputer.Speaker.tone(1000, 150);
        lastStatusMs = 0;
    }
    if (idNoticeMs && now - idNoticeMs > 15000) {
        idNoticeMs = 0;
        ui::setNotice("");
        lastStatusMs = 0;
    }

    int dimAfter = atoi(config::get("dim_after", "60").c_str());
    if (dimAfter > 0 && !dimmed && now - lastKeyMs > (uint32_t)dimAfter * 1000) {
        M5Cardputer.Display.setBrightness(12);
        dimmed = true;
    }

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
