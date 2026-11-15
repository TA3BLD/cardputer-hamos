#include "Ble.h"
#include <Arduino.h>
#include <deque>
#include <string>
#include "Lang.h"
#include "lib/Cat.h"

#ifdef HAMOS_SIM
// ---- simulator: a radio that is always there ----
std::deque<uint8_t> sim_ble_from_radio;     // the script puts the radio's answers here
std::string sim_ble_to_radio;               // what HamOS wrote

namespace ble {
namespace {
bool on = false;
}
void ic705Start() { on = true; }
void ic705Stop() { on = false; }
Link ic705State() { return on ? Link::Ready : Link::Off; }
std::string ic705Status() { return on ? "IC-705 connected" : ""; }
bool ic705Write(const uint8_t* d, size_t n) {
    if (!on) return false;
    sim_ble_to_radio.append((const char*)d, n);
    return true;
}
size_t ic705Read(uint8_t* buf, size_t max) {
    size_t n = 0;
    while (on && n < max && !sim_ble_from_radio.empty()) {
        buf[n++] = sim_ble_from_radio.front();
        sim_ble_from_radio.pop_front();
    }
    return n;
}
}  // namespace ble

#else
#include <BLEDevice.h>

namespace ble {

namespace {

bool stackUp = false;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
std::deque<uint8_t> rx;
volatile Link state = Link::Off;
volatile bool wanted = false, granted = false, disconnected = false;
volatile int pairReply = -1;
BLEClient* client = nullptr;
BLERemoteCharacteristic* chr = nullptr;
TaskHandle_t task = nullptr;
std::string statusText;
uint16_t chunk = 20;

void stack() {
    if (stackUp) return;
    BLEDevice::init("HamOS");
    stackUp = true;
}

void onNotify(BLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
    int r = cat::ic705::handshakeReply(d, n);
    if (r == 0x64) { granted = true; return; }
    if (r == 0x63 && n >= 6) { pairReply = d[4]; return; }
    if (r) return;
    portENTER_CRITICAL(&mux);
    for (size_t i = 0; i < n; i++) rx.push_back(d[i]);
    while (rx.size() > 2048) rx.pop_front();
    portEXIT_CRITICAL(&mux);
}

class Callbacks : public BLEClientCallbacks {
    void onConnect(BLEClient*) override {}
    void onDisconnect(BLEClient*) override {
        disconnected = true;
        granted = false;
    }
};
Callbacks callbacks;

void write(const std::vector<uint8_t>& v) {
    if (chr) chr->writeValue((uint8_t*)v.data(), v.size(), false);
}

// Looks for the radio, connects, says hello; then waits while it stays connected.
void run(void*) {
    BLEUUID service(cat::ic705::kService), characteristic(cat::ic705::kChar);
    while (wanted) {
        if (state == Link::Ready && !disconnected) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        chr = nullptr;
        if (client) {
            if (client->isConnected()) client->disconnect();
        } else {
            client = BLEDevice::createClient();
            client->setClientCallbacks(&callbacks);
        }
        disconnected = false;
        granted = false;
        pairReply = -1;
        state = Link::Searching;
        statusText = T("Looking for the IC-705...");
        BLEScan* scan = BLEDevice::getScan();
        scan->setActiveScan(true);
        BLEScanResults found = scan->start(5, false);
        BLEAdvertisedDevice* radio = nullptr;
        static BLEAdvertisedDevice keep;
        for (int i = 0; i < found.getCount(); i++) {
            BLEAdvertisedDevice d = found.getDevice(i);
            if (d.haveServiceUUID() && d.isAdvertisingService(service)) {
                keep = d;
                radio = &keep;
                break;
            }
        }
        scan->clearResults();
        if (!radio) {
            statusText = T("IC-705 not found: Bluetooth on?");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        state = Link::Connecting;
        statusText = T("Connecting to the IC-705...");
        if (!client->connect(radio)) {
            statusText = T("IC-705 did not accept the connection");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        client->setMTU(185);
        uint16_t mtu = client->getMTU();
        chunk = mtu > 23 ? mtu - 3 : 20;
        BLERemoteService* svc = client->getService(service);
        BLERemoteCharacteristic* c = svc ? svc->getCharacteristic(characteristic) : nullptr;
        if (!c) {
            client->disconnect();
            statusText = T("IC-705 serial service missing");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (c->canNotify()) c->registerForNotify(onNotify);
        chr = c;
        state = Link::Pairing;
        statusText = T("Pairing: on the radio allow HamOS");
        uint64_t mac = ESP.getEfuseMac();
        write(cat::ic705::sendId(cat::ic705::deviceId(mac)));
        vTaskDelay(pdMS_TO_TICKS(50));
        write(cat::ic705::sendName("HamOS-Cardputer"));
        vTaskDelay(pdMS_TO_TICKS(50));
        write(cat::ic705::sendToken());
        for (int i = 0; i < 300 && !granted && !disconnected; i++) vTaskDelay(pdMS_TO_TICKS(100));   // up to 30 s to accept
        if (granted) {
            state = Link::Ready;
            statusText = T("IC-705 connected");
        } else {
            statusText = T("IC-705: open Bluetooth > Pairing Reception on the radio");
            chr = nullptr;
            client->disconnect();
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
    if (client && client->isConnected()) client->disconnect();
    chr = nullptr;
    state = Link::Off;
    statusText.clear();
    task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

void ic705Start() {
    if (wanted) return;
    stack();
    wanted = true;
    if (!task) xTaskCreatePinnedToCore(run, "ic705", 6144, nullptr, 1, &task, 0);
}

void ic705Stop() { wanted = false; }
Link ic705State() { return state; }
std::string ic705Status() { return statusText; }

bool ic705Write(const uint8_t* d, size_t n) {
    if (state != Link::Ready || !chr) return false;
    for (size_t off = 0; off < n; off += chunk) {
        size_t len = n - off < chunk ? n - off : chunk;
        chr->writeValue((uint8_t*)d + off, len, false);
    }
    return true;
}

size_t ic705Read(uint8_t* buf, size_t max) {
    size_t n = 0;
    portENTER_CRITICAL(&mux);
    while (n < max && !rx.empty()) {
        buf[n++] = rx.front();
        rx.pop_front();
    }
    portEXIT_CRITICAL(&mux);
    return n;
}

}  // namespace ble
#endif
