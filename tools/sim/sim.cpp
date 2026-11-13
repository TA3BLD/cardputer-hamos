// Runs the real HamOS code on a PC, presses keys from a script and records
// what would be drawn. See run.sh.
#include "main.cpp"

uint32_t sim_ms = 1000;

static void run(int ms = 60) {
    for (int t = 0; t < ms; t += 10) {
        sim_ms += 10;
        loop();
    }
}
static void press(const Keyboard_Class::KeysState& s) {
    M5Cardputer.Keyboard.inject(s);
    run();
}
static void key(char c) { Keyboard_Class::KeysState s; s.word.push_back(c); press(s); }
static void type(const char* text) { for (const char* p = text; *p; p++) key(*p); }
static void enter() { Keyboard_Class::KeysState s; s.enter = true; press(s); }
static void tab() { Keyboard_Class::KeysState s; s.tab = true; press(s); }
static void del(int n = 1) { while (n--) { Keyboard_Class::KeysState s; s.del = true; press(s); } }
static void back() { key('`'); }
static void shot(const char* name) { run(1100); printf("S %s\n", name); }

static void logQso(const char* call, const char* freq, const char* mode, const char* note) {
    key('n');
    type(call); enter();
    del(10); type(freq); enter();
    del(8); type(mode); enter();
    enter(); enter();
    type(note); enter();
}

int main() {
    setup();
    run();
    shot("launcher");

    enter();                                   // Logbook (first tile)
    logQso("dl1abc", "14.205", "ssb", "");
    logQso("g4xyz", "14.074", "ft8", "");
    logQso("ta1kk", "145.500", "fm", "Istanbul");
    logQso("w1aw", "7.030", "cw", "");
    shot("logbook");
    key('n'); type("w1aw"); enter(); enter(); enter(); enter(); enter(); type("again");
    shot("logbook-new-qso");
    back(); back();

    key('/'); enter();                         // Locator
    type("FN31pr");
    shot("locator");
    back();

    key('/'); enter();                         // Calculator
    shot("calculator-list");
    key('.'); enter();
    shot("calculator-swr");
    back(); back();

    key('/'); enter();                         // Morse
    type("cq de ta3bld");
    shot("morse-send");
    tab(); run(3000);
    shot("morse-train");
    back();

    key('/'); enter();                         // UTC Clock
    shot("clock");
    back();

    key('/'); enter();                         // Reference
    shot("reference");
    back();

    key('/'); enter();                         // Settings
    shot("settings");
    return 0;
}
