/**
 * @file VehicleLink.h
 * @brief Byte links of the vehicle sources: UART on the UART header (GPIO 43 / 44) and an ELM327 BLE adapter
 * @see Vehicle.h
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

/** The saved source was OBD over BLE at boot, so the BLE controller memory was kept (bleInUse()). */
bool vehicleBleKeptAtBoot();

class VehicleLink {
public:
    virtual ~VehicleLink() = default;
    /** Connect (may block for seconds: BLE scan). False = not now (status() says why). */
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() = 0;
    virtual size_t read(uint8_t* buf, size_t cap) = 0;
    virtual bool write(const uint8_t* data, size_t n) = 0;
    virtual const char* status() = 0;
};

/** UART1 on the UART header: TX GPIO 43, RX GPIO 44 (USB-CDC is the console, so UART0's pins are free). */
class UartLink : public VehicleLink {
public:
    static constexpr int kTx = 43, kRx = 44;
    explicit UartLink(uint32_t baud, bool pullUpRx = false) : _baud(baud), _pullUp(pullUpRx) {}
    bool open() override;
    void close() override;
    bool isOpen() override { return _open; }
    size_t read(uint8_t* buf, size_t cap) override;
    bool write(const uint8_t* data, size_t n) override;
    const char* status() override { return _open ? "UART" : "UART OFF"; }
    /** K-Line wake-up: TX low `lowMs`, high `highMs`, then the UART again. Blocks for both. */
    void wakePulse(uint32_t lowMs, uint32_t highMs);

private:
    uint32_t _baud;
    bool _pullUp;
    bool _open = false;
};

/**
 * ELM327 Bluetooth LE adapter (Vgate iCar Pro BLE, Veepeak BLE, Konnwei, "V-LINK", most "OBDII" BLE
 * clones). Scans for a name / service that looks like one (or uses the saved address), then talks over its
 * serial-like characteristic pair: FFF0 (FFF1 notify, FFF2 write), FFE0 (FFE1), 18F0 (2AF0 / 2AF1),
 * Nordic UART, or any notify + write pair it finds. Classic-Bluetooth (SPP) adapters cannot work: the
 * ESP32-S3 has Bluetooth LE only.
 */
class BleElmLink : public VehicleLink {
public:
    /** `addr` / `type`: saved adapter (updated after a scan finds one: check foundAddress()). */
    BleElmLink(const char* addr, uint8_t type);
    ~BleElmLink() override;
    bool open() override;
    void close() override;
    bool isOpen() override;
    size_t read(uint8_t* buf, size_t cap) override;
    bool write(const uint8_t* data, size_t n) override;
    const char* status() override { return _status; }
    /** Address of the adapter a scan found ("" if none / it was already saved). */
    const char* foundAddress() const { return _found; }
    uint8_t foundType() const { return _foundType; }

private:
    char _addr[18] = "";
    uint8_t _type = 0;
    char _found[18] = "";
    uint8_t _foundType = 0;
    const char* _status = "BT";
    StreamBufferHandle_t _rx = nullptr;
    void* _client = nullptr;  // BLEClient*
    void* _writeChar = nullptr;  // BLERemoteCharacteristic*
    bool _writeWithResponse = false;
};
