// Declarations copied from arduino-esp32 2.0.17 libraries/BLE (signatures only), for a syntax check.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <cstdint>
typedef int esp_err_t;
typedef int esp_ble_gatts_cb_param_t;
struct portMUX_TYPE { int x; };
#define portMUX_INITIALIZER_UNLOCKED {0}
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
typedef void* TaskHandle_t;
typedef uint32_t BaseType_t;
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int) { return 1; }
inline void vTaskDelay(uint32_t) {}
inline void vTaskDelete(void*) {}
#define pdMS_TO_TICKS(x) (x)
class BLEUUID { public: BLEUUID(const char*); BLEUUID(std::string); };
class BLEAddress {};
class BLERemoteCharacteristic;
typedef std::function<void(BLERemoteCharacteristic*, uint8_t*, size_t, bool)> notify_callback;
class BLERemoteCharacteristic {
public:
    bool canNotify();
    void registerForNotify(notify_callback _callback, bool notifications = true, bool descriptorRequiresRegistration = true);
    void writeValue(uint8_t* data, size_t length, bool response = false);
};
class BLERemoteService { public: BLERemoteCharacteristic* getCharacteristic(BLEUUID uuid); };
class BLEAdvertisedDevice {
public:
    bool haveServiceUUID();
    bool isAdvertisingService(BLEUUID uuid);
};
class BLEScanResults { public: int getCount(); BLEAdvertisedDevice getDevice(uint32_t i); };
class BLEScan {
public:
    void setActiveScan(bool active);
    BLEScanResults start(uint32_t duration, bool is_continue = false);
    void clearResults();
};
class BLEClient;
class BLEClientCallbacks { public: virtual ~BLEClientCallbacks() {}; virtual void onConnect(BLEClient*) = 0; virtual void onDisconnect(BLEClient*) = 0; };
class BLEClient {
public:
    bool connect(BLEAdvertisedDevice* device);
    void disconnect();
    BLERemoteService* getService(BLEUUID uuid);
    bool isConnected();
    void setClientCallbacks(BLEClientCallbacks* p);
    uint16_t getMTU();
    bool setMTU(uint16_t mtu);
};
class BLEDescriptor {};
class BLECharacteristic;
class BLECharacteristicCallbacks {
public:
    virtual ~BLECharacteristicCallbacks();
    virtual void onRead(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param);
    virtual void onRead(BLECharacteristic* pCharacteristic);
    virtual void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param);
    virtual void onWrite(BLECharacteristic* pCharacteristic);
};
class BLECharacteristic {
public:
    static const uint32_t PROPERTY_READ = 1 << 0, PROPERTY_WRITE = 1 << 1, PROPERTY_NOTIFY = 1 << 2, PROPERTY_WRITE_NR = 1 << 5;
    uint8_t* getData();
    size_t getLength();
    void notify(bool is_notification = true);
    void setCallbacks(BLECharacteristicCallbacks* pCallbacks);
    void setValue(uint8_t* data, size_t size);
    void setValue(std::string value);
    void setValue(uint16_t& data16);
    void setValue(uint32_t& data32);
    void setValue(int& data32);
    void addDescriptor(BLEDescriptor* pDescriptor);
};
class BLEService {
public:
    BLECharacteristic* createCharacteristic(const char* uuid, uint32_t properties);
    BLECharacteristic* createCharacteristic(BLEUUID uuid, uint32_t properties);
    void start();
};
class BLEServer;
class BLEServerCallbacks {
public:
    virtual ~BLEServerCallbacks() {};
    virtual void onConnect(BLEServer* pServer);
    virtual void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param);
    virtual void onDisconnect(BLEServer* pServer);
    virtual void onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param);
};
class BLEServer {
public:
    BLEService* createService(const char* uuid);
    BLEService* createService(BLEUUID uuid, uint32_t numHandles = 15, uint8_t inst_id = 0);
    void setCallbacks(BLEServerCallbacks* pCallbacks);
    void disconnect(uint16_t connId);
    uint16_t getConnId();
};
class BLEAdvertisementData {
public:
    void setCompleteServices(BLEUUID uuid);
    void setFlags(uint8_t);
    void setName(std::string name);
};
#define ESP_BLE_ADV_FLAG_GEN_DISC (0x01 << 1)
#define ESP_BLE_ADV_FLAG_BREDR_NOT_SPT (0x01 << 2)
class BLEAdvertising {
public:
    void addServiceUUID(const char* serviceUUID);
    void setScanResponse(bool);
    void setAdvertisementData(BLEAdvertisementData& advertisementData);
    void setScanResponseData(BLEAdvertisementData& advertisementData);
    void setMinPreferred(uint16_t);
    void setMaxPreferred(uint16_t);
};
class BLEDevice {
public:
    static BLEClient* createClient();
    static BLEServer* createServer();
    static BLEScan* getScan();
    static void init(std::string deviceName);
    static esp_err_t setMTU(uint16_t mtu);
    static BLEAdvertising* getAdvertising();
    static bool getInitialized();
    static void startAdvertising();
    static void stopAdvertising();
};
