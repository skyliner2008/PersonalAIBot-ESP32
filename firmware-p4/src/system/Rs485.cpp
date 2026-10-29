/**
 * @file Rs485.cpp
 * @brief RS485 Modbus RTU master (see Rs485.h)
 */

#include "Rs485.h"

#include <ArduinoJson.h>

#include "../util/DeviceConfig.h"
#include "../util/JsonAlloc.h"
#include "../util/JsonFile.h"
#include "../vehicle/VehicleLink.h"
#include "Peripherals.h"

Rs485 gRs485;

namespace {
constexpr uint32_t kReplyTimeoutMs = 300;  // first byte; a slow meter answers in 50-150 ms
constexpr uint32_t kByteGapMs = 20;        // silence inside a reply = it is over
constexpr int kTries = 2;                  // a timeout / CRC error is asked once more

HardwareSerial& port() { return Serial2; }  // UART1 = the vehicle link (VehicleLink.cpp)

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m); }
};

bool serialConfig(const char* f, uint32_t& out) {
    if (!strcmp(f, "8N1")) return (out = SERIAL_8N1), true;
    if (!strcmp(f, "8E1")) return (out = SERIAL_8E1), true;
    if (!strcmp(f, "8O1")) return (out = SERIAL_8O1), true;
    if (!strcmp(f, "8N2")) return (out = SERIAL_8N2), true;
    return false;
}
}  // namespace

void Rs485::begin(const char* agentDir) {
    if (!_lock) _lock = xSemaphoreCreateMutex();
    if (!agentDir || !*agentDir) return;
    snprintf(_path, sizeof(_path), "%s/rs485.json", agentDir);
    JsonDocument d(JsonPsramAllocator::instance());
    if (!jsonfile::load(_path, d)) return;  // not set yet: the port stays off
    Rs485Config c;
    c.tx = d["tx"] | -1;
    c.rx = d["rx"] | -1;
    c.de = d["de"] | -1;
    c.baud = d["baud"] | 9600u;
    snprintf(c.format, sizeof(c.format), "%s", d["format"] | "8N1");
    _cfg = c;
    if (open()) {
        Serial.printf("[RS485] Modbus RTU on TX %d / RX %d, %lu %s%s\n", _cfg.tx, _cfg.rx, (unsigned long)_cfg.baud,
                      _cfg.format, _cfg.de >= 0 ? " (DE pin)" : "");
    } else {
        Serial.printf("[RS485] %s: setting not usable - port off\n", _path);
    }
}

const char* Rs485::configure(const Rs485Config& c) {
    uint32_t fmt;
    if (!serialConfig(c.format, fmt)) return "format must be 8N1, 8E1, 8O1 or 8N2";
    if (c.baud < 1200 || c.baud > 115200) return "baud must be 1200..115200";
    if (c.tx < 0 || c.rx < 0 || c.tx == c.rx || (c.de >= 0 && (c.de == c.tx || c.de == c.rx))) return "TX, RX (and DE) must be different pins";
    for (int pin : {c.tx, c.rx, c.de}) {
        if (pin < 0) continue;
        if (const char* e = periph::pinProblem(periph::Kind::Switch, pin)) return e;
        if (gPeriph.usesPin(pin)) return "that pin belongs to a device in devices.json";
    }
    if (c.tx == UartLink::kTx || c.rx == UartLink::kRx || c.tx == UartLink::kRx || c.rx == UartLink::kTx) {
        Serial.println("[RS485] note: same pins as the vehicle UART (J5) - do not use the OBD UART source at the same time");
    }
    if (!_path[0]) return "no storage for the setting (SD card / flash)";
    {
        Guard g(_lock);
        close();
        _cfg = c;
        if (!open()) return "cannot open the UART on these pins";
    }
    if (!save()) return "opened, but the setting could not be saved";
    return nullptr;
}

void Rs485::disable() {
    {
        Guard g(_lock);
        close();
        _cfg = Rs485Config();
    }
    if (_path[0]) ::remove(_path);
}

bool Rs485::open() {
    uint32_t fmt;
    if (_cfg.tx < 0 || _cfg.rx < 0 || !serialConfig(_cfg.format, fmt)) return false;
    port().setRxBufferSize(512);
    port().begin(_cfg.baud, fmt, _cfg.rx, _cfg.tx);
    // RS485 half duplex like the vendor's uart_echo_rs485 example; the board's MAX485 turns around by itself (DE/RE
    // from TX1 through U7/U9), a DE pin is only for other adapters - the UART then drives it as RTS
    if (_cfg.de >= 0) port().setPins(_cfg.rx, _cfg.tx, -1, _cfg.de);
    port().setMode(UART_MODE_RS485_HALF_DUPLEX);
    _open = true;
    return true;
}

void Rs485::close() {
    if (_open) port().end();
    _open = false;
}

bool Rs485::save() {
    JsonDocument d(JsonPsramAllocator::instance());
    d["tx"] = _cfg.tx;
    d["rx"] = _cfg.rx;
    d["de"] = _cfg.de;
    d["baud"] = _cfg.baud;
    d["format"] = (const char*)_cfg.format;
    return jsonfile::save(_path, d);
}

size_t Rs485::transact(const uint8_t* req, size_t reqLen, uint8_t* rx, size_t cap) {
    // RTU: a frame starts after 3.5 character times of silence
    const uint32_t gapUs = modbus::interFrameUs(_cfg.baud);
    const uint32_t since = (micros() - _lastEnd);
    if (since < gapUs) delayMicroseconds(gapUs - since);
    while (port().available()) port().read();  // a late reply to an earlier request
    port().write(req, reqLen);
    port().flush();  // all bits out (the MAX485 turns around by itself)
    size_t n = 0;
    uint32_t last = millis();
    const uint32_t start = last;
    for (;;) {
        while (port().available() && n < cap) {
            rx[n++] = (uint8_t)port().read();
            last = millis();
        }
        const size_t want = modbus::replyLength(rx, n);
        if (want && n >= want) break;
        if (n == 0 && millis() - start > kReplyTimeoutMs) break;
        if (n > 0 && millis() - last > kByteGapMs) break;
        if (n >= cap) break;
        vTaskDelay(1);
    }
    _lastEnd = micros();
    return n;
}

const char* Rs485::read(uint8_t slave, modbus::Table t, uint16_t addr, uint16_t count, uint16_t* values) {
    if (!_open) return "RS485 is not set up (console: rs485 set <tx> <rx> <baud>)";
    uint8_t req[8];
    if (!modbus::buildRead(slave, t, addr, count, req)) return "bad request (slave 1..247, count 1..64)";
    uint8_t rx[modbus::kMaxFrame];
    const char* err = nullptr;
    Guard g(_lock);
    for (int i = 0; i < kTries; i++) {
        const size_t n = transact(req, sizeof(req), rx, sizeof(rx));
        err = modbus::parseRead(rx, n, slave, t, count, values);
        if (!err || strncmp(err, "exception", 9) == 0) break;  // the device answered: asking again changes nothing
    }
    _done++;
    if (err) _failed++;
    return err;
}

const char* Rs485::write(uint8_t slave, modbus::Table t, uint16_t addr, const uint16_t* values, uint16_t count) {
    if (!_open) return "RS485 is not set up (console: rs485 set <tx> <rx> <baud>)";
    uint8_t req[9 + 2 * modbus::kMaxCount];
    size_t len = 0;
    if (count == 1) len = modbus::buildWriteSingle(slave, t, addr, values[0], req);
    else if (t == modbus::Table::Holding) len = modbus::buildWriteMultiple(slave, addr, values, count, req);
    if (!len) return "bad request (write: coil or holding, slave 1..247, up to 64 registers)";
    uint8_t rx[16];
    const char* err = nullptr;
    Guard g(_lock);
    for (int i = 0; i < kTries; i++) {
        const size_t n = transact(req, len, rx, sizeof(rx));
        err = modbus::parseWrite(rx, n, req);
        if (!err || strncmp(err, "exception", 9) == 0 || n > 0) break;  // never write twice once it answered
    }
    _done++;
    if (err) _failed++;
    return err;
}
