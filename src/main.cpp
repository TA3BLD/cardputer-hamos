// HamOS - amateur radio toolkit for the M5Stack Cardputer / Cardputer ADV.
#include <M5Cardputer.h>
#include <vector>

#include "apps/AprsApp.h"
#include "apps/AudioLevelApp.h"
#include "apps/AwardsApp.h"
#include "apps/CalcApp.h"
#include "apps/CallbookApp.h"
#include "apps/ClockApp.h"
#include "apps/ContestApp.h"
#include "apps/CountryApp.h"
#include "apps/CwKeyerApp.h"
#include "apps/DigiApp.h"
#include "apps/DxClusterApp.h"
#include "apps/EmergencyApp.h"
#include "apps/Ft8App.h"
#include "apps/WsjtxApp.h"
#include "apps/GpsApp.h"
#include "apps/GuideApp.h"
#include "apps/Launcher.h"
#include "apps/LocatorApp.h"
#include "apps/LogbookApp.h"
#include "apps/LogSyncApp.h"
#include "apps/MapApp.h"
#include "apps/MeshApp.h"
#include "apps/MorseApp.h"
#include "apps/PskApp.h"
#include "apps/PotaApp.h"
#include "apps/ProfilesApp.h"
#include "apps/PskrApp.h"
#include "apps/RadioApp.h"
#include "apps/RangeApp.h"
#include "apps/SelfTestApp.h"
#include "apps/SetupApp.h"
#include "apps/ReferenceApp.h"
#include "apps/RepeaterApp.h"
#include "apps/SatApp.h"
#include "apps/SettingsApp.h"
#include "apps/SolarApp.h"
#include "apps/SstvApp.h"
#include "apps/UpdateApp.h"
#include "apps/WatchApp.h"
#include "apps/WxApp.h"
#include "apps/VoiceApp.h"
#include "core/AprsIs.h"
#include "core/Audio.h"
#include "core/Alarms.h"
#include "core/Ble.h"
#include "core/Cap.h"
#include "core/Config.h"
#include "core/Emergency.h"
#include "core/Gps.h"
#include "core/Keyer.h"
#include "core/Lang.h"
#include "core/LoraAprs.h"
#include "core/Mesh.h"
#include "core/Net.h"
#include "core/Openings.h"
#include "core/RangeTest.h"
#include "core/PskReporter.h"
#include "core/Rig.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "core/Updater.h"
#include "core/Usb.h"
#include "core/Watch.h"
#include "core/Weather.h"
#include "core/Voice.h"
#include "core/Web.h"
#include "core/Wsjtx.h"

// ---- App registry: add new apps here ---------------------------------------
static LogbookApp logbookApp;
static MeshApp meshApp;
static PotaApp potaApp;
static PskrApp pskrApp;
static CallbookApp callbookApp;
static LogSyncApp logSyncApp;
static RadioApp radioApp;
static CwKeyerApp cwKeyerApp;
static VoiceApp voiceApp;
static RangeApp rangeApp;
static WxApp wxApp;
static EmergencyApp emergencyApp;
static AwardsApp awardsApp;
static ContestApp contestApp;
static SetupApp setupApp;
static ProfilesApp profilesApp;
static SelfTestApp selfTestApp;
static AudioLevelApp audioLevelApp;
static WatchApp watchApp;
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
static UpdateApp updateApp;
// Each app with its launcher group, in launcher order.
struct Entry {
    App* app;
    int group;
};
static const Entry registry[] = {
        {&logbookApp, Launcher::LOG},
        {&meshApp, Launcher::MESH},
        {&aprsApp, Launcher::MESH},
        {&mapApp, Launcher::MESH},
        {&ft8App, Launcher::DIGITAL},
        {&dxClusterApp, Launcher::RADIO},
        {&potaApp, Launcher::RADIO},
        {&pskrApp, Launcher::RADIO},
        {&callbookApp, Launcher::LOG},
        {&logSyncApp, Launcher::LOG},
        {&radioApp, Launcher::RADIO},
        {&cwKeyerApp, Launcher::RADIO},
        {&voiceApp, Launcher::RADIO},
        {&rangeApp, Launcher::MESH},
        {&wxApp, Launcher::MESH},
        {&contestApp, Launcher::LOG},
        {&awardsApp, Launcher::LOG},
        {&wsjtxApp, Launcher::DIGITAL},
        {&solarApp, Launcher::RADIO},
        {&satApp, Launcher::RADIO},
        {&gpsApp, Launcher::TOOLS},
        {&digiApp, Launcher::DIGITAL},
        {&sstvApp, Launcher::DIGITAL},
        {&pskApp, Launcher::DIGITAL},
        {&locatorApp, Launcher::TOOLS},
        {&repeaterApp, Launcher::RADIO},
        {&calcApp, Launcher::TOOLS},
        {&morseApp, Launcher::TOOLS},
        {&clockApp, Launcher::TOOLS},
        {&referenceApp, Launcher::INFO},
        {&guideApp, Launcher::INFO},
        {&countryApp, Launcher::INFO},
        {&settingsApp, Launcher::TOOLS},
        {&updateApp, Launcher::TOOLS},
        {&emergencyApp, Launcher::TOOLS},
        {&selfTestApp, Launcher::TOOLS},
        {&audioLevelApp, Launcher::DIGITAL},
        {&profilesApp, Launcher::TOOLS},
        {&setupApp, Launcher::TOOLS},
        {&watchApp, Launcher::RADIO},
};
static std::vector<App*> listApps() {
    std::vector<App*> v;
    for (const Entry& e : registry) v.push_back(e.app);
    return v;
}
static std::vector<int> listGroups() {
    std::vector<int> v;
    for (const Entry& e : registry) v.push_back(e.group);
    return v;
}
static const std::vector<App*> apps = listApps();
static const std::vector<int> appGroups = listGroups();
static Launcher launcher(apps, appGroups);
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
    if (dimmed && !emergency::ownsBacklight()) {
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
    bool countdown = emergency::state() == emergency::State::Countdown;
    emergency::keyPressed();                // resets the safety timer; cancels the SOS countdown
    if (countdown) {                        // that key only cancelled the countdown
        emergencyApp.dirty = true;
        return;
    }
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

// ---- control from a computer over USB ----
static void usbKey(const KeyEvent& k) {
    noteActivity();
    dispatch(k);
}
static void usbOpen(App* app) {
    noteActivity();
    if (app != current) switchTo(app ? app : &launcher);
}
static App* usbCurrent() { return current; }
static void redrawAll() {
    ui::fill(0, 0, ui::W, ui::H, ui::C_BG);
    current->dirty = true;
    lastStatusMs = 0;
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
    updater::bootGuard();   // may switch back to the previous firmware after failed starts
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    cap::begin();           // before the SD card: parks the radio on the shared SPI bus
    ui::begin();
    ui::textCentered(50, T("HamOS"), ui::C_TEXT, 3);

    storage::begin();
    config::load();
    lang::reload();         // the language setting is known now
    net::begin();
    gps::begin();
    mesh::begin();
    rangetest::begin();
    loraaprs::begin();
    aprsis::begin();
    rig::begin();
    weather::begin();
    wsjtxlink::begin();
    if (config::get("mesh_ble", "0") == "1") ble::meshStart();
    emergency::begin();
    M5Cardputer.Display.setBrightness(brightness());
    noteActivity();

    usb::Host usbHost = {usbKey, usbOpen, usbCurrent, &apps, redrawAll};
    usb::begin(usbHost);

    std::string call = config::get("callsign");
    if (!call.empty()) ui::textCentered(85, T("73 de ") + call, ui::C_OK, 1.5);
    delay(800);
    M5Cardputer.Speaker.setVolume((uint8_t)atoi(config::get("volume", "160").c_str()));
    // First start: no callsign yet -> the setup wizard.
    if (config::get("callsign").empty() && config::get("setup_done") != "1") current = &setupApp;
    current->onEnter();
}

void loop() {
    M5Cardputer.update();
    net::tick();
    gps::tick();
    mesh::tick();
    loraaprs::tick();
    web::tick();
    usb::tick();
    updater::tick(current == &launcher);
    pskr::tick();
    ble::tick();
    rangetest::tick();
    weather::tick();
    emergency::tick();
    watch::tick(current == &launcher);
    openings::tick(current == &launcher);
    // Watch list hits, band openings, alarms: a beep and a line in the status bar for 30 s.
    {
        static uint32_t infoUntil = 0;
        std::string info = watch::takeNotice();
        if (info.empty()) info = openings::takeNotice();
        std::string alarm = alarms::tick();
        if (!alarm.empty()) {
            info = alarm;
            M5Cardputer.Speaker.tone(2000, 300);
        }
        if (!info.empty() && emergency::state() != emergency::State::Active) {
            ui::setNotice(info);
            infoUntil = millis() + 30000;
            lastStatusMs = 0;
        } else if (infoUntil && (int32_t)(millis() - infoUntil) > 0) {
            infoUntil = 0;
            if (emergency::state() != emergency::State::Active) ui::setNotice("");
            lastStatusMs = 0;
        }
    }
    // Emergency from any screen: hold the G0 side button for 2 seconds.
    static bool g0Held = false;
    if (M5Cardputer.BtnA.pressedFor(2000)) {
        if (!g0Held && current != &emergencyApp) {
            noteActivity();
            switchTo(&emergencyApp);
        }
        g0Held = true;
    } else if (!M5Cardputer.BtnA.isPressed()) {
        g0Held = false;
    }
    // While an SOS runs, every screen says so (the SOS goes on in the background).
    static bool sosNotice = false;
    bool sosOn = emergency::state() == emergency::State::Active;
    if (sosOn != sosNotice) {
        sosNotice = sosOn;
        ui::setNotice(sosOn ? T("SOS ACTIVE") : "");
        lastStatusMs = 0;
    }
    // Someone else calls for help: show it, whatever is open.
    if (emergency::takeNewDistress()) {
        noteActivity();
        if (current != &emergencyApp) switchTo(&emergencyApp);
        emergencyApp.showAlerts();
        M5Cardputer.Speaker.tone(2200, 700);
    }
    aprsis::tick();
    rig::tick();
    wsjtxlink::tick();
    audio::tick();
    keyer::tick();
    voice::tick();
    // Digital modes and APRS transmit through the speaker: key the radio meanwhile.
    static bool audioPtt = false;
    if (audio::transmitting() != audioPtt) {
        audioPtt = audio::transmitting();
        if (config::get("ptt_audio", "1") == "1") rig::ptt(audioPtt);
    }
    pollKeyboard();

    uint32_t now = millis();
    current->onTick(now);

    // Station identification reminder ("id_timer" minutes, 0 = off).
    static uint32_t lastIdMs = 0, idNoticeMs = 0;
    int idMinutes = atoi(config::get("id_timer", "0").c_str());
    if (idMinutes > 0 && now - lastIdMs > (uint32_t)idMinutes * 60000) {
        lastIdMs = now;
        idNoticeMs = now ? now : 1;
        ui::setNotice(T("ID: give your callsign"));
        M5Cardputer.Speaker.tone(1000, 150);
        lastStatusMs = 0;
    }
    if (idNoticeMs && now - idNoticeMs > 15000) {
        idNoticeMs = 0;
        ui::setNotice("");
        lastStatusMs = 0;
    }

    // A new version on GitHub: say so once; installing is up to the user (Update app).
    static bool updateAnnounced = false;
    if (!updateAnnounced && updater::status().state == updater::State::Available) {
        updateAnnounced = true;
        ui::setNotice(std::string(T("Update available")) + ": " + updater::status().latest.version);
        lastStatusMs = 0;
        if (current == &launcher) launcher.dirty = true;
    }
    if (updateAnnounced && current == &updateApp && idNoticeMs == 0) ui::setNotice("");

    int dimAfter = atoi(config::get("dim_after", "60").c_str());
    if (dimAfter > 0 && !dimmed && !emergency::ownsBacklight() && now - lastKeyMs > (uint32_t)dimAfter * 1000) {
        M5Cardputer.Display.setBrightness(12);
        dimmed = true;
    }

    if (lastStatusMs == 0 || now - lastStatusMs >= 1000) {
        lastStatusMs = now ? now : 1;
        ui::statusBar(current->name());
    }
    if (current == &setupApp && setupApp.finished) {
        setupApp.finished = false;
        switchTo(&launcher);
    }
    if (current->dirty) {
        current->dirty = false;
        current->draw();
    }
    delay(5);
}
