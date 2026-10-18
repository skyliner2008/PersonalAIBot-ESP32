/**
 * @file HondaKline.h
 * @brief Honda PGM-FI diagnostic K-Line (motorcycles: Wave 110i / 125i, Click, PCX, MSX, CBR ...):
 *        framing, checksum, wake-up / hello / table polling and table decoding - host tested
 * @see wiki/16-Vehicle-Dash.md
 *
 * Single-wire bus at 10400 baud through an L9637D-type transceiver on the UART pins (GPIO 43 TX / 44 RX).
 *   wake-up   K-Line low 70 ms, high 130 ms, then FE 04 FF FF (no answer)
 *   hello     72 05 00 F0 99           -> 02 04 00 FA
 *   table     72 05 71 <table> <cs>    -> 02 <len> 71 <table> <data ...> <cs>
 * A frame is [type][length of the whole frame][...][checksum]; all its bytes add up to 0 (mod 256).
 * The transceiver echoes what we send: those bytes are skipped.
 *
 * Which byte of a table holds what differs between models. Layout below is the common "table 0x11"
 * arrangement - check it on your bike with the raw dump (console `dash kdump`) and change it in
 * agent/vehicle.json ({"kline": {"table": 17, "rpm": 0, "coolant": 5, ...}}, -1 = not read).
 */

#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "VehicleData.h"

namespace hondak {

constexpr uint32_t kBaud = 10400;
constexpr uint32_t kWakeLowMs = 70, kWakeHighMs = 130;

/** Two's complement of the byte sum: appended, it makes the frame add up to 0. */
uint8_t checksum(const uint8_t* p, size_t n);
/** 72 05 71 <table> <cs> into out[5]. */
void tableRequest(uint8_t table, uint8_t out[5]);

struct Layout {
    uint8_t table = 0x11;
    // byte offsets in the table data (after "71 <table>"); -1 = not read
    int8_t rpm = 0;       // 2 bytes, big endian
    int8_t throttle = -1; // raw * 100 / 255
    int8_t coolant = 5;   // raw - 40 (air-cooled engines: oil / head temperature)
    int8_t intake = 7;    // raw - 40
    int8_t map = 9;       // kPa
    int8_t battery = 12;  // raw / 10 V
    int8_t speed = 13;    // km/h (0 on models whose speedometer is mechanical)
};

/** Overrides from {"table": 17, "rpm": 0, ...}; out-of-range values keep the default. */
void parseLayout(JsonObjectConst o, Layout& l);
/** Table data -> readings. False if it is shorter than the RPM field needs. */
bool decode(const Layout& l, const uint8_t* data, size_t n, vehicle::Snapshot& out, uint32_t now);

/** Protocol state; the link (UART + the wake-up pulse on TX) is driven by the caller. */
class Session {
public:
    enum class Phase : uint8_t { Wake, Hello, Poll };
    struct Action {
        enum Kind : uint8_t { None, WakePulse, Send } kind = None;
        const uint8_t* bytes = nullptr;
        size_t n = 0;
    };

    void setLayout(const Layout& l) { _layout = l; }
    const Layout& layout() const { return _layout; }
    void restart(uint32_t now);
    /** What to do now: pull the line low/high (then call wakeDone), send bytes, or nothing. */
    Action poll(uint32_t now);
    void wakeDone(uint32_t now);
    /** Bytes from the transceiver (echo included). */
    void feed(const uint8_t* b, size_t n, vehicle::Snapshot& out, uint32_t now);

    Phase phase() const { return _phase; }
    const char* status() const;
    uint32_t frames() const { return _frames; }
    /** The last table reply as hex ("" before the first), for working out a model's layout. */
    const char* lastDump() const { return _dump; }

    static constexpr uint32_t kReplyTimeoutMs = 300;
    static constexpr uint32_t kPollGapMs = 40;   // between a reply and the next request (~15 tables/s)
    static constexpr uint8_t kMaxMisses = 5;     // then wake up again

private:
    Layout _layout;
    Phase _phase = Phase::Wake;
    bool _waiting = false, _helloSent = false;
    uint32_t _sentAt = 0, _nextAt = 0;
    uint8_t _misses = 0;
    uint32_t _frames = 0;
    uint8_t _tx[8] = {};
    size_t _txLen = 0, _echoLeft = 0, _echoPos = 0;
    uint8_t _rx[72] = {};
    size_t _rxLen = 0;
    char _dump[150] = "";

    Action send(const uint8_t* b, size_t n, uint32_t now, bool expectReply);
    void handleFrame(const uint8_t* f, size_t n, vehicle::Snapshot& out, uint32_t now);
};

}  // namespace hondak
