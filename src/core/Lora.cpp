#include "Lora.h"
#include <M5Cardputer.h>
#include <RadioLib.h>
#include <SPI.h>
#include <cstdlib>
#include "Cap.h"
#include "Config.h"
#include "Keyer.h"

namespace lora {

static Module* module = nullptr;
static SX1262* radio = nullptr;
static bool isReady = false, transmitting = false;
static std::string lastError = "not started";
static RxHandler handler = nullptr;
static volatile bool irq = false;
static uint32_t txStartMs = 0;

static void IRAM_ATTR onDio1() { irq = true; }

bool begin(const Settings& s) {
    isReady = transmitting = false;
    if (keyer::paddleUsesCapPins()) {   // no cap: the paddle has its pins
        lastError = "LoRa pins used by the paddle";
        return false;
    }
    if (!radio) {
        module = new Module(cap::LORA_CS, cap::LORA_DIO1, cap::LORA_RST, cap::LORA_BUSY, SPI);
        radio = new SX1262(module);
    }
    // The cap's SX1262 module has a 1.8 V TCXO on DIO3. If the chip does not
    // start with it, fall back to a plain crystal (the same thing the
    // Meshtastic firmware does). "lora_tcxo" in config.txt overrides this.
    float tcxo = (float)atof(config::get("lora_tcxo", "1.8").c_str());
    int16_t err = radio->begin(s.freqMhz, s.bwKhz, s.sf, s.cr, s.syncWord, s.powerDbm, s.preamble, tcxo, false);
    if (err != RADIOLIB_ERR_NONE && tcxo != 0)
        err = radio->begin(s.freqMhz, s.bwKhz, s.sf, s.cr, s.syncWord, s.powerDbm, s.preamble, 0, false);
    if (err != RADIOLIB_ERR_NONE) {
        lastError = "LoRa radio not found (error " + std::to_string(err) + ")";
        return false;
    }
    radio->setDio2AsRfSwitch(true);
    radio->setCurrentLimit(140);
    radio->setRxBoostedGainMode(true);
    radio->setCRC(2);
    radio->setDio1Action(onDio1);
    irq = false;
    err = radio->startReceive();
    if (err != RADIOLIB_ERR_NONE) {
        lastError = "LoRa receive failed (error " + std::to_string(err) + ")";
        return false;
    }
    lastError.clear();
    isReady = true;
    return true;
}

bool ready() { return isReady; }
std::string error() { return lastError; }
void onReceive(RxHandler h) { handler = h; }
bool busy() { return transmitting; }

bool send(const uint8_t* data, size_t len) {
    if (!isReady || transmitting || len == 0 || len > 255) return false;
    irq = false;
    if (radio->startTransmit(data, len) != RADIOLIB_ERR_NONE) {
        radio->startReceive();
        return false;
    }
    transmitting = true;
    txStartMs = millis();
    return true;
}

void tick() {
    if (!isReady) return;
    if (transmitting && millis() - txStartMs > 15000) irq = true;   // never get stuck in transmit
    if (!irq) return;
    irq = false;
    if (transmitting) {
        radio->finishTransmit();
        transmitting = false;
    } else {
        uint8_t buf[256];
        size_t len = radio->getPacketLength();
        if (len > 0 && len <= 255 && radio->readData(buf, len) == RADIOLIB_ERR_NONE && handler)
            handler(buf, len, radio->getRSSI(), radio->getSNR());
    }
    radio->startReceive();
}

}  // namespace lora
