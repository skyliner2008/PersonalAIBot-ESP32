/**
 * @file Rs485.h
 * @brief The board's RS485 port (MAX485, automatic direction) as a Modbus RTU master - the `modbus` tool (T131)
 * @see ../../../wiki/19-ESP32-P4-JC4880P443C.md
 *
 * The board's MAX485 is on GPIO 26 / 27 (schematic JC4880P443_V1.0 page 5, shared with the UART J5): console
 * `rs485 set <baud> [8N1|8E1|8O1|8N2]` (or `rs485 set <tx> <rx> <baud> [fmt] [de]` for another transceiver) ->
 * "<agentDir>/rs485.json". Until it is set the port is off and the tool is not declared (the same pins can also be
 * the vehicle UART source). A DE pin is only for adapters without automatic direction (-1 = none).
 * UART2 (UART1 is the vehicle link). Transactions are serialised by a mutex (agent task tool calls, console).
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../util/ModbusRtu.h"

struct Rs485Config {
    int tx = -1, rx = -1, de = -1;
    uint32_t baud = 9600;
    char format[4] = "8N1";  // 8N1 / 8E1 / 8O1 / 8N2
};

class Rs485 {
public:
    /** Loads "<dir>/rs485.json" and opens the port when it is set. */
    void begin(const char* agentDir);
    bool ready() const { return _open; }
    const Rs485Config& config() const { return _cfg; }

    /** Check, save and (re)open. nullptr = ok, else why not (pins in use, bad format...). */
    const char* configure(const Rs485Config& c);
    void disable();

    /** Read `count` values; `values` gets registers or 0 / 1 for bits. nullptr = ok, else why not. */
    const char* read(uint8_t slave, modbus::Table t, uint16_t addr, uint16_t count, uint16_t* values);
    /** Write one coil / holding register, or several holding registers (count > 1). nullptr = ok. */
    const char* write(uint8_t slave, modbus::Table t, uint16_t addr, const uint16_t* values, uint16_t count);

    /** Transactions / failures since boot (diag). */
    uint32_t transactions() const { return _done; }
    uint32_t failures() const { return _failed; }

private:
    Rs485Config _cfg;
    char _path[96] = "";
    bool _open = false;
    SemaphoreHandle_t _lock = nullptr;
    uint32_t _done = 0, _failed = 0, _lastEnd = 0;

    bool open();
    void close();
    bool save();
    /** Send `req`, collect the reply (length from its header) within the timeout. Returns the bytes received. */
    size_t transact(const uint8_t* req, size_t reqLen, uint8_t* rx, size_t cap);
};

extern Rs485 gRs485;
