/**
 * @file VehicleLink.cpp
 * @brief UART and BLE links of the vehicle sources (see VehicleLink.h), and the BLE memory decision at boot
 */

#include "VehicleLink.h"

#include <driver/gpio.h>

#include "VehicleData.h"
#include "VehicleLog.h"

#if VEHICLE_BLE
#include <BLEDevice.h>
#include <stdarg.h>
#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#include <esp_bt.h>
#endif
#include <esp_heap_caps.h>
#include <nvs.h>

// ---------------------------------------------------------------- BLE memory at boot

namespace {
bool gBleKept = false;
}

// initArduino() (before setup, NVS already up) frees the BLE controller memory unless this says BLE is in
// use. Only a saved "OBD over BLE" source keeps it: everyone else keeps those ~36 KB of internal RAM.
extern "C" bool bleInUse(void) {
    nvs_handle_t h;
    uint8_t src = 0;
    if (nvs_open("veh", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "src", &src);
        nvs_close(h);
    }
    gBleKept = src == (uint8_t)vehicle::SourceId::ObdBle;
    return gBleKept;
}

bool vehicleBleKeptAtBoot() { return gBleKept; }

// T113: the prebuilt SDK gives NimBLE's host pools (msys / ACL / HCI events, ~20 KB) internal RAM only
// (CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL). The BLE build links with -Wl,--wrap=nimble_mem_malloc,... (env
// esp32s3-ble in platformio.ini): PSRAM first, like CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL. In IRAM like the originals.
extern "C" {
IRAM_ATTR void* __wrap_nimble_mem_malloc(size_t size) {
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
IRAM_ATTR void* __wrap_nimble_mem_calloc(size_t n, size_t size) {
    return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
IRAM_ATTR void __wrap_nimble_mem_free(void* p) { heap_caps_free(p); }

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
// The controller is set up for 6 BLE activities and a 100-device duplicate list (the SDK defaults): the ELM327
// needs one scan or one connection. Fewer = less of the controller's internal RAM (-Wl,--wrap=esp_bt_controller_init)
esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t* cfg);
esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t* cfg) {
    if (cfg) {
        if (cfg->ble_max_act > 3) cfg->ble_max_act = 3;
        if (cfg->normal_adv_size > 20) cfg->normal_adv_size = 20;
        if (cfg->ble_adv_dup_filt_max > 10) cfg->ble_adv_dup_filt_max = 10;
    }
    return __real_esp_bt_controller_init(cfg);
}
#endif
}
#else
// no BLE library linked: Arduino's own bleInUse() is false and releases the controller memory at boot
bool vehicleBleKeptAtBoot() { return false; }
#endif  // VEHICLE_BLE

// ---------------------------------------------------------------- UART

bool UartLink::open() {
    if (_open) return true;
    if (kTx < 0 || kRx < 0) return false;  // ESP32-P4 board: UART header pins not known yet (pins_config.h)
    Serial1.setRxBufferSize(1024);
    Serial1.begin(_baud, SERIAL_8N1, kRx, kTx);
    if (_pullUp) gpio_pullup_en((gpio_num_t)kRx);  // L9637D-type RX is open collector
    _open = true;
    return true;
}

void UartLink::close() {
    if (!_open) return;
    Serial1.end();
    _open = false;
}

size_t UartLink::read(uint8_t* buf, size_t cap) {
    if (!_open) return 0;
    const int a = Serial1.available();
    if (a <= 0) return 0;
    return Serial1.readBytes(buf, (size_t)a < cap ? (size_t)a : cap);
}

bool UartLink::write(const uint8_t* data, size_t n) {
    if (!_open) return false;
    return Serial1.write(data, n) == n;
}

void UartLink::wakePulse(uint32_t lowMs, uint32_t highMs) {
    if (kTx < 0) return;
    close();
    pinMode(kTx, OUTPUT);
    digitalWrite(kTx, LOW);
    vTaskDelay(pdMS_TO_TICKS(lowMs));
    digitalWrite(kTx, HIGH);
    vTaskDelay(pdMS_TO_TICKS(highMs));
    open();
}

// ---------------------------------------------------------------- BLE ELM327

#if VEHICLE_BLE
namespace {

constexpr size_t kMinInternalHeap = 40 * 1024;  // NimBLE host + a connection need about this much
bool gBleUp = false;
bool gBleFailed = false;  // BLEDevice::init failed: not again until a restart (the controller is half up)

/** A line on the console and in the SD card's OBD log (T115). */
void bleNote(const char* fmt, ...) {
    char text[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    Serial.printf("[Vehicle] BLE: %s\n", text);
    gVehicleLog.recordf('E', "BLE: %s", text);
}

// BLEDevice::init reads NVS (the bond store), which a task with a PSRAM stack (the vehicle task, T113) must not:
// a short-lived task with an internal stack runs it (and deinit, T114). Static: a timed-out job still finishes into it.
struct BleJob {
    void (*fn)() = nullptr;
    volatile bool done = true, ok = false;
};
BleJob gBleJob;

void bleJobTask(void*) {
    gBleJob.fn();
    gBleJob.done = true;
    vTaskDelete(nullptr);
}

bool runBleJob(void (*fn)()) {
    if (!gBleJob.done) return false;  // the last one never finished
    gBleJob.fn = fn;
    gBleJob.ok = false;
    gBleJob.done = false;
    if (xTaskCreatePinnedToCore(bleJobTask, "blejob", 5120, nullptr, 3, nullptr, 0) != pdPASS) {
        gBleJob.done = true;
        return false;
    }
    for (int i = 0; i < 1500 && !gBleJob.done; i++) vTaskDelay(pdMS_TO_TICKS(10));  // up to 15 s
    return gBleJob.done;
}

bool startBle() {
    return runBleJob([]() { gBleJob.ok = BLEDevice::init("PersonalAIBot") && BLEDevice::getInitialized(); }) && gBleJob.ok;
}

bool looksLikeObd(BLEAdvertisedDevice& d) {
    String n = d.getName();
    n.toUpperCase();
    static const char* const kNames[] = {"OBD", "ELM", "VLINK", "V-LINK", "VEEPEAK", "KONNWEI", "VGATE", "ICAR", "CARISTA"};
    for (const char* k : kNames) {
        if (n.indexOf(k) >= 0) return true;
    }
    static const uint16_t kServices[] = {0xFFF0, 0xFFE0, 0x18F0};
    for (uint16_t s : kServices) {
        if (d.isAdvertisingService(BLEUUID(s))) return true;
    }
    return false;
}

struct Pair {
    const char* service;
    const char* notify;
    const char* write;
};
const Pair kPairs[] = {
    {"fff0", "fff1", "fff2"},
    {"ffe0", "ffe1", "ffe1"},
    {"18f0", "2af0", "2af1"},
    {"6e400001-b5a3-f393-e0a9-e50e24dcca9e", "6e400003-b5a3-f393-e0a9-e50e24dcca9e", "6e400002-b5a3-f393-e0a9-e50e24dcca9e"},
    {"e7810a71-73ae-499d-8c15-faa9aef0c3f2", "bef8d6c9-9c21-4c9e-b632-bd58c1009f9f", "bef8d6c9-9c21-4c9e-b632-bd58c1009f9f"},
};

}  // namespace

bool vehicleBleRunning() { return gBleUp; }

// T114: Bluetooth off (the dash closed): controller + NimBLE host stopped, their internal RAM back to the assistant.
// The BT memory kept at boot stays reserved, so it can start again. Close the link first.
void vehicleBleShutdown() {
    if (!gBleUp) return;
    // BLEDevice keeps a pointer to the last client it made and deletes it in deinit - BleElmLink::close() deleted that
    // one already: a fresh client replaces the stale pointer, deinit deletes this one instead of freeing twice
    BLEDevice::createClient();
    runBleJob([]() { BLEDevice::deinit(false); });
    gBleUp = false;
}

BleElmLink::BleElmLink(const char* addr, uint8_t type) {
    if (addr) snprintf(_addr, sizeof(_addr), "%s", addr);
    _type = type;
    _rx = xStreamBufferCreate(1024, 1);
}

BleElmLink::~BleElmLink() {
    close();
    if (_rx) vStreamBufferDelete(_rx);
}

bool BleElmLink::isOpen() { return _client && static_cast<BLEClient*>(_client)->isConnected() && _writeChar; }

void BleElmLink::close() {
    if (_client) {
        BLEClient* c = static_cast<BLEClient*>(_client);
        if (c->isConnected()) c->disconnect();
        delete c;
        _client = nullptr;
    }
    _writeChar = nullptr;
    if (_rx) xStreamBufferReset(_rx);
}

bool BleElmLink::open() {
    if (isOpen()) return true;
    close();
    if (!vehicleBleKeptAtBoot()) {
        _status = "BT RESTART";  // the BLE memory was given back at boot: one restart with this source
        return false;
    }
    if (gBleFailed) {
        _status = "BT FAIL";
        return false;
    }
    if (!gBleUp) {
        const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (before < kMinInternalHeap) {
            _status = "BT NO RAM";
            bleNote("only %u KB internal RAM free - not starting Bluetooth", (unsigned)(before / 1024));
            return false;
        }
        // T113: a failed init ("hci inits failed") left the stack half up and the scan below crashed the board on
        // every boot (the source is saved): stop here, until the next boot
        if (!startBle()) {
            gBleFailed = true;
            _status = "BT FAIL";
            bleNote("Bluetooth did not start (%u KB internal RAM free before, %u KB now)", (unsigned)(before / 1024),
                    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
            return false;
        }
        gBleUp = true;
        bleNote("Bluetooth started - %u KB internal RAM free", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    }
    BLEClient* client = BLEDevice::createClient();
    _client = client;
    bool connected = false;
    if (_addr[0]) {
        _status = "BT CONNECT";
        bleNote("connecting to %s", _addr);
        connected = client->connect(BLEAddress(String(_addr), _type), _type, 8000);
    }
    if (!connected) {
        _status = "BT SCAN";
        bleNote("scanning 6 s for an OBD adapter...");
        BLEScan* scan = BLEDevice::getScan();
        scan->setActiveScan(true);
        // T116: window 99 of 100 ms left Wi-Fi (the assistant's voice) almost no airtime for 6 s - one radio for both
        scan->setInterval(100);
        scan->setWindow(30);
        BLEScanResults* res = scan->start(6, false);
        int best = -1, bestRssi = -999;
        for (int i = 0; res && i < res->getCount(); i++) {
            BLEAdvertisedDevice d = res->getDevice(i);
            const bool obd = looksLikeObd(d);
            bleNote("  %s  %-20s rssi %d%s", d.getAddress().toString().c_str(), d.getName().c_str(), d.getRSSI(),
                    obd ? "  <- OBD?" : "");
            if (obd && d.getRSSI() > bestRssi) {
                best = i;
                bestRssi = d.getRSSI();
            }
        }
        if (best >= 0) {
            BLEAdvertisedDevice d = res->getDevice(best);
            _status = "BT CONNECT";
            connected = client->connect(&d);
            if (connected) {
                snprintf(_found, sizeof(_found), "%s", d.getAddress().toString().c_str());
                _foundType = d.getAddressType();
                snprintf(_addr, sizeof(_addr), "%s", _found);
                _type = _foundType;
            }
        }
        scan->clearResults();
    }
    if (!connected) {
        _status = "NO ADAPTER";
        bleNote("no OBD adapter connected");
        close();
        return false;
    }
    // the adapter's serial-like characteristics: a known pair, else the first notify + write it has
    BLERemoteCharacteristic* notifyC = nullptr;
    BLERemoteCharacteristic* writeC = nullptr;
    for (const Pair& p : kPairs) {
        BLERemoteService* svc = client->getService(BLEUUID(String(p.service)));
        if (!svc) continue;
        notifyC = svc->getCharacteristic(BLEUUID(String(p.notify)));
        writeC = svc->getCharacteristic(BLEUUID(String(p.write)));
        if (notifyC && writeC) break;
        notifyC = writeC = nullptr;
    }
    if (!notifyC || !writeC) {
        std::map<std::string, BLERemoteService*>* services = client->getServices();
        for (auto& sv : *services) {
            std::map<std::string, BLERemoteCharacteristic*>* chars = sv.second->getCharacteristics();
            for (auto& ch : *chars) {
                if (!notifyC && (ch.second->canNotify() || ch.second->canIndicate())) notifyC = ch.second;
                if (!writeC && (ch.second->canWrite() || ch.second->canWriteNoResponse())) writeC = ch.second;
            }
            if (notifyC && writeC) break;
            notifyC = writeC = nullptr;  // both from the same service
        }
    }
    if (!notifyC || !writeC) {
        _status = "BT NO SERIAL";
        bleNote("connected, but no notify + write characteristic pair found");
        close();
        return false;
    }
    StreamBufferHandle_t rx = _rx;
    notifyC->registerForNotify([rx](BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
        xStreamBufferSend(rx, data, len, 0);  // NimBLE host task: never block it
    });
    _writeChar = writeC;
    _writeWithResponse = !writeC->canWriteNoResponse();
    _status = "BT OK";
    bleNote("%s connected (notify %s, write %s)", _addr, notifyC->getUUID().toString().c_str(),
            writeC->getUUID().toString().c_str());
    return true;
}

size_t BleElmLink::read(uint8_t* buf, size_t cap) { return _rx ? xStreamBufferReceive(_rx, buf, cap, 0) : 0; }

bool BleElmLink::write(const uint8_t* data, size_t n) {
    if (!isOpen()) return false;
    return static_cast<BLERemoteCharacteristic*>(_writeChar)->writeValue(const_cast<uint8_t*>(data), n, _writeWithResponse);
}
#endif  // VEHICLE_BLE
