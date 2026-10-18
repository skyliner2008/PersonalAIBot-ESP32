/**
 * @file Elm327.h
 * @brief OBD-II through an ELM327 (or STN / clone) adapter: init sequence, PID polling and reply parsing,
 *        independent of the link (BLE or UART) - host tested
 * @see wiki/16-Vehicle-Dash.md
 *
 * The adapter is a text device: a command ends in '\r', the reply ends with the '>' prompt. After
 * ATZ / ATE0 (no echo) / ATL0 / ATS0 (no spaces) / ATH0 (no headers) / ATAT1 / ATSP0 (find the protocol),
 * "0100" asks the car which PIDs it supports; then the supported ones of
 *   010C rpm = (256A + B) / 4      010D speed = A km/h       0105 coolant = A - 40   010F intake = A - 40
 *   0111 throttle = A * 100 / 255  010B MAP = A kPa          ATRV battery voltage at the OBD socket
 * are polled in turn, rpm every other request so the bar stays quick.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "VehicleData.h"

namespace vehicle {

class Elm327 {
public:
    enum class Phase : uint8_t { Reset, Init, Search, Poll, NoEcu };

    /** Start over (new link, or after errors). */
    void restart(uint32_t now);

    /**
     * The command to send now (without '\r'), or nullptr while a reply is awaited. A command that got no
     * prompt in time is reported by timeouts() and sent again; after kMaxTimeouts the init starts over.
     */
    const char* poll(uint32_t now);

    /** Bytes from the adapter. Complete replies update `out`. */
    void feed(const char* data, size_t n, Snapshot& out, uint32_t now);

    Phase phase() const { return _phase; }
    /** Short status for the dash ("ELM INIT", "ECU SEARCH", "NO ECU", "LIVE"). */
    const char* status() const;
    /** Adapter id from ATZ ("ELM327 v1.5"). */
    const char* adapter() const { return _adapter; }
    uint32_t timeouts() const { return _timeouts; }
    bool supports(uint8_t pid) const;

    /** Decode one reply line of mode 01 ("410C1AF8", spaces allowed) into `out`. False if it is not one. */
    static bool decodeMode01(const char* line, Snapshot& out, uint32_t now);
    /** "12.6V" -> 12.6 (ATRV). */
    static bool parseVoltage(const char* line, float& volts);

    static constexpr uint32_t kMaxTimeouts = 3;

private:
    Phase _phase = Phase::Reset;
    uint8_t _initStep = 0;
    uint8_t _pollIndex = 0;
    bool _waiting = false;
    uint32_t _sentAt = 0, _retryAt = 0, _lastVolt = 0;
    uint32_t _timeouts = 0;
    uint32_t _supported = 0;  // PIDs 0x01-0x20 (bit 31 = PID 01, as the car sends them)
    char _cmd[12] = "";
    char _line[96] = "";
    size_t _lineLen = 0;
    char _reply[160] = "";    // all lines of the current reply, '\n'-separated
    size_t _replyLen = 0;
    char _adapter[24] = "";

    void send(const char* cmd, uint32_t now);
    void handleReply(Snapshot& out, uint32_t now);
    uint32_t replyTimeout() const;
};

}  // namespace vehicle
