#pragma once
// Signatures as in arduino-esp32 2.0.x libraries/BLE/src/BLESecurity.h
#include <cstdint>
typedef uint8_t esp_ble_auth_req_t;
typedef uint8_t esp_ble_io_cap_t;
#define ESP_LE_AUTH_REQ_SC_BOND 0x09
#define ESP_IO_CAP_NONE 3
#define ESP_BLE_ENC_KEY_MASK (1 << 0)
#define ESP_BLE_ID_KEY_MASK (1 << 1)
class BLESecurity {
public:
    BLESecurity();
    void setAuthenticationMode(esp_ble_auth_req_t auth_req);
    void setCapability(esp_ble_io_cap_t iocap);
    void setInitEncryptionKey(uint8_t init_key);
};
