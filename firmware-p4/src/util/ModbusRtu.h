/**
 * @file ModbusRtu.h
 * @brief Modbus RTU master frames for the RS485 port: CRC-16, requests, reply framing and checks
 * @see ../../../wiki/19-ESP32-P4-JC4880P443C.md (T131)
 *
 * Header-only and portable (host tests: ../host_tests/test_assistant.cpp includes it by path). Function codes
 * 01 / 02 / 03 / 04 (read coils, discrete inputs, holding, input registers), 05 / 06 (write one coil / register) and
 * 16 (write registers). A read returns at most kMaxCount values - enough for what the assistant reads aloud.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace modbus {

enum class Table : uint8_t { Coil, Discrete, Holding, Input };

constexpr uint16_t kMaxCount = 64;      // values in one read (the spec allows 125 registers / 2000 bits)
constexpr size_t kMaxFrame = 5 + 128 + 2;  // the longest reply we ask for: 64 registers = 128 data bytes

/** "holding" / "input" / "coil" / "discrete" (also "hr", "ir", "co", "di"). False = unknown. */
inline bool parseTable(const char* s, Table& t) {
    if (!s) return false;
    if (!strcmp(s, "holding") || !strcmp(s, "hr")) return (t = Table::Holding), true;
    if (!strcmp(s, "input") || !strcmp(s, "ir")) return (t = Table::Input), true;
    if (!strcmp(s, "coil") || !strcmp(s, "co")) return (t = Table::Coil), true;
    if (!strcmp(s, "discrete") || !strcmp(s, "di")) return (t = Table::Discrete), true;
    return false;
}

inline const char* tableName(Table t) {
    switch (t) {
        case Table::Coil: return "coil";
        case Table::Discrete: return "discrete";
        case Table::Holding: return "holding";
        default: return "input";
    }
}

inline bool isBits(Table t) { return t == Table::Coil || t == Table::Discrete; }

/** CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF); sent low byte first. */
inline uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

inline size_t finish(uint8_t* f, size_t n) {
    const uint16_t c = crc16(f, n);
    f[n] = (uint8_t)(c & 0xFF);
    f[n + 1] = (uint8_t)(c >> 8);
    return n + 2;
}

inline uint8_t readFunction(Table t) {
    switch (t) {
        case Table::Coil: return 0x01;
        case Table::Discrete: return 0x02;
        case Table::Holding: return 0x03;
        default: return 0x04;
    }
}

/** Read request (8 bytes into `out`). 0 = bad arguments (slave 1..247, count 1..kMaxCount, no address wrap). */
inline size_t buildRead(uint8_t slave, Table t, uint16_t addr, uint16_t count, uint8_t* out) {
    if (slave < 1 || slave > 247 || count < 1 || count > kMaxCount || (uint32_t)addr + count > 0x10000) return 0;
    out[0] = slave;
    out[1] = readFunction(t);
    out[2] = (uint8_t)(addr >> 8), out[3] = (uint8_t)addr;
    out[4] = (uint8_t)(count >> 8), out[5] = (uint8_t)count;
    return finish(out, 6);
}

/** Write one coil (05: any value != 0 = ON) or one holding register (06). 0 = bad arguments / read-only table. */
inline size_t buildWriteSingle(uint8_t slave, Table t, uint16_t addr, uint16_t value, uint8_t* out) {
    if (slave < 1 || slave > 247 || (t != Table::Coil && t != Table::Holding)) return 0;
    out[0] = slave;
    out[1] = t == Table::Coil ? 0x05 : 0x06;
    if (t == Table::Coil) value = value ? 0xFF00 : 0x0000;
    out[2] = (uint8_t)(addr >> 8), out[3] = (uint8_t)addr;
    out[4] = (uint8_t)(value >> 8), out[5] = (uint8_t)value;
    return finish(out, 6);
}

/** Write holding registers (16). `out` needs 9 + 2 * count bytes. 0 = bad arguments. */
inline size_t buildWriteMultiple(uint8_t slave, uint16_t addr, const uint16_t* values, uint16_t count, uint8_t* out) {
    if (slave < 1 || slave > 247 || count < 1 || count > kMaxCount || (uint32_t)addr + count > 0x10000) return 0;
    out[0] = slave;
    out[1] = 0x10;
    out[2] = (uint8_t)(addr >> 8), out[3] = (uint8_t)addr;
    out[4] = (uint8_t)(count >> 8), out[5] = (uint8_t)count;
    out[6] = (uint8_t)(count * 2);
    for (uint16_t i = 0; i < count; i++) {
        out[7 + 2 * i] = (uint8_t)(values[i] >> 8);
        out[8 + 2 * i] = (uint8_t)values[i];
    }
    return finish(out, 7 + 2 * (size_t)count);
}

/**
 * Bytes the reply has once `n` bytes of it are in (0 = not known yet: read on). An exception reply (function | 0x80)
 * is 5 bytes; a read reply 3 + its byte count + CRC; a write echo 8.
 */
inline size_t replyLength(const uint8_t* rx, size_t n) {
    if (n < 2) return 0;
    const uint8_t fc = rx[1];
    if (fc & 0x80) return 5;
    if (fc >= 0x01 && fc <= 0x04) return n < 3 ? 0 : 5 + (size_t)rx[2];
    if (fc == 0x05 || fc == 0x06 || fc == 0x10) return 8;
    return 5;  // unknown: as short as possible, the checks below refuse it
}

inline const char* exceptionText(uint8_t code) {
    switch (code) {
        case 1: return "exception 1: illegal function (the device has no such function)";
        case 2: return "exception 2: illegal data address (no such register on the device)";
        case 3: return "exception 3: illegal data value";
        case 4: return "exception 4: slave device failure";
        case 5: return "exception 5: acknowledge (busy, try again later)";
        case 6: return "exception 6: slave device busy";
        case 10: return "exception 10: gateway path unavailable";
        case 11: return "exception 11: gateway target device failed to respond";
        default: return "exception: unknown code";
    }
}

/** Common checks of a complete reply: length, CRC, slave, function, exception. nullptr = fine. */
inline const char* checkReply(const uint8_t* rx, size_t n, uint8_t slave, uint8_t function) {
    if (n == 0) return "no reply (timeout) - check wiring A/B, slave address, baud rate";
    const size_t want = replyLength(rx, n);
    if (want == 0 || n < want) return "reply cut short";
    if (crc16(rx, want - 2) != (uint16_t)(rx[want - 2] | (rx[want - 1] << 8))) return "bad CRC (noise, wrong baud / parity)";
    if (rx[0] != slave) return "reply from another slave address";
    if (rx[1] == (function | 0x80)) return exceptionText(rx[2]);
    if (rx[1] != function) return "reply to another function";
    return nullptr;
}

/** A read reply into `values` (registers, or 0 / 1 for bits). nullptr = ok, else why not. */
inline const char* parseRead(const uint8_t* rx, size_t n, uint8_t slave, Table t, uint16_t count, uint16_t* values) {
    if (const char* e = checkReply(rx, n, slave, readFunction(t))) return e;
    const size_t bytes = rx[2];
    if (isBits(t)) {
        if (bytes != (size_t)(count + 7) / 8) return "reply has a different number of values";
        for (uint16_t i = 0; i < count; i++) values[i] = (rx[3 + i / 8] >> (i % 8)) & 1;
    } else {
        if (bytes != (size_t)count * 2) return "reply has a different number of values";
        for (uint16_t i = 0; i < count; i++) values[i] = (uint16_t)(rx[3 + 2 * i] << 8 | rx[4 + 2 * i]);
    }
    return nullptr;
}

/** A write reply (05 / 06 echo the request; 16 echoes address + count). nullptr = ok. */
inline const char* parseWrite(const uint8_t* rx, size_t n, const uint8_t* request) {
    if (const char* e = checkReply(rx, n, request[0], request[1])) return e;
    if (memcmp(rx + 2, request + 2, 4) != 0) return "the device confirmed another address / value";
    return nullptr;
}

/** 3.5 character times of silence between frames (RTU), at least 1750 us above 19200 baud (the spec). */
inline uint32_t interFrameUs(uint32_t baud, uint8_t bitsPerChar = 11) {
    if (baud > 19200) return 1750;
    return (uint32_t)((35ULL * bitsPerChar * 1000000ULL) / (10ULL * baud)) + 1;
}

}  // namespace modbus
