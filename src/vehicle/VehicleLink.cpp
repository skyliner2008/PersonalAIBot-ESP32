/**
 * @file VehicleLink.cpp
 * @brief UART and BLE links of the vehicle sources (see VehicleLink.h), and the BLE memory decision at boot
 */

#include "VehicleLink.h"

#include <BLEDevice.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <nvs.h>

#include "VehicleData.h"

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

// ---------------------------------------------------------------- UART

bool UartLink::open() {
    if (_open) return true;
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
    close();
    pinMode(kTx, OUTPUT);
    digitalWrite(kTx, LOW);
    vTaskDelay(pdMS_TO_TICKS(lowMs));
    digitalWrite(kTx, HIGH);
    vTaskDelay(pdMS_TO_TICKS(highMs));
    open();
}

// ---------------------------------------------------------------- BLE ELM327

namespace {

constexpr size_t kMinInternalHeap = 40 * 1024;  // NimBLE host + a connection need about this much
bool gBleUp = false;

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
    if (!gBleUp) {
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < kMinInternalHeap) {
            _status = "BT NO RAM";
            Serial.printf("[Vehicle] BLE: only %u KB internal RAM free - not starting Bluetooth\n",
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
            return false;
        }
        BLEDevice::init("PersonalAIBot");
        gBleUp = true;
    }
    BLEClient* client = BLEDevice::createClient();
    _client = client;
    bool connected = false;
    if (_addr[0]) {
        _status = "BT CONNECT";
        Serial.printf("[Vehicle] BLE: connecting to %s\n", _addr);
        connected = client->connect(BLEAddress(String(_addr), _type), _type, 8000);
    }
    if (!connected) {
        _status = "BT SCAN";
        Serial.println("[Vehicle] BLE: scanning 6 s for an OBD adapter...");
        BLEScan* scan = BLEDevice::getScan();
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(99);
        BLEScanResults* res = scan->start(6, false);
        int best = -1, bestRssi = -999;
        for (int i = 0; res && i < res->getCount(); i++) {
            BLEAdvertisedDevice d = res->getDevice(i);
            const bool obd = looksLikeObd(d);
            Serial.printf("[Vehicle]   %s  %-20s rssi %d%s\n", d.getAddress().toString().c_str(), d.getName().c_str(),
                          d.getRSSI(), obd ? "  <- OBD?" : "");
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
        Serial.println("[Vehicle] BLE: connected, but no notify + write characteristic pair found");
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
    Serial.printf("[Vehicle] BLE: %s connected (notify %s, write %s)\n", _addr, notifyC->getUUID().toString().c_str(),
                  writeC->getUUID().toString().c_str());
    return true;
}

size_t BleElmLink::read(uint8_t* buf, size_t cap) { return _rx ? xStreamBufferReceive(_rx, buf, cap, 0) : 0; }

bool BleElmLink::write(const uint8_t* data, size_t n) {
    if (!isOpen()) return false;
    return static_cast<BLERemoteCharacteristic*>(_writeChar)->writeValue(const_cast<uint8_t*>(data), n, _writeWithResponse);
}
