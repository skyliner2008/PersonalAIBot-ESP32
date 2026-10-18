/**
 * @file HondaKline.cpp
 * @brief Honda K-Line protocol (see HondaKline.h)
 */

#include "HondaKline.h"

#include <stdio.h>
#include <string.h>

namespace hondak {

namespace {
const uint8_t kWake[] = {0xFE, 0x04, 0xFF, 0xFF};
const uint8_t kHello[] = {0x72, 0x05, 0x00, 0xF0, 0x99};
constexpr uint32_t kAfterWakeMs = 200;  // the ECU ignores a hello sent right after the wake-up bytes

void setOffset(JsonObjectConst o, const char* key, int8_t& field) {
    if (!o[key].is<int>()) return;
    const int v = o[key].as<int>();
    if (v >= -1 && v < 60) field = (int8_t)v;
}
}  // namespace

uint8_t checksum(const uint8_t* p, size_t n) {
    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++) sum = (uint8_t)(sum + p[i]);
    return (uint8_t)(0x100 - sum);
}

void tableRequest(uint8_t table, uint8_t out[5]) {
    out[0] = 0x72;
    out[1] = 0x05;
    out[2] = 0x71;
    out[3] = table;
    out[4] = checksum(out, 4);
}

void parseLayout(JsonObjectConst o, Layout& l) {
    if (o.isNull()) return;
    if (o["table"].is<int>()) {
        const int t = o["table"].as<int>();
        if (t >= 0 && t <= 0xFF) l.table = (uint8_t)t;
    }
    setOffset(o, "rpm", l.rpm);
    setOffset(o, "throttle", l.throttle);
    setOffset(o, "coolant", l.coolant);
    setOffset(o, "intake", l.intake);
    setOffset(o, "map", l.map);
    setOffset(o, "battery", l.battery);
    setOffset(o, "speed", l.speed);
}

bool decode(const Layout& l, const uint8_t* d, size_t n, vehicle::Snapshot& out, uint32_t now) {
    using namespace vehicle;
    if (l.rpm < 0 || (size_t)l.rpm + 1 >= n) return false;
    out.set(kRpm, (float)(d[l.rpm] << 8 | d[l.rpm + 1]), now);
    auto byteAt = [&](int8_t off, int& v) {
        if (off < 0 || (size_t)off >= n) return false;
        v = d[off];
        return true;
    };
    int v;
    if (byteAt(l.throttle, v)) out.set(kThrottle, v * 100.0f / 255.0f, now);
    if (byteAt(l.coolant, v)) out.set(kCoolant, v - 40.0f, now);
    if (byteAt(l.intake, v)) out.set(kIntake, v - 40.0f, now);
    if (byteAt(l.map, v)) out.set(kMap, (float)v, now);
    if (byteAt(l.battery, v)) out.set(kBattery, v / 10.0f, now);
    if (byteAt(l.speed, v)) out.set(kSpeed, (float)v, now);
    return true;
}

// ---------------------------------------------------------------- session

void Session::restart(uint32_t now) {
    _phase = Phase::Wake;
    _waiting = _helloSent = false;
    _misses = 0;
    _rxLen = _echoLeft = 0;
    _nextAt = now;
}

Session::Action Session::send(const uint8_t* b, size_t n, uint32_t now, bool expectReply) {
    memcpy(_tx, b, n);
    _txLen = n;
    _echoLeft = n;  // the transceiver hands these back first
    _echoPos = 0;
    _rxLen = 0;
    _waiting = expectReply;
    _sentAt = now;
    Action a;
    a.kind = Action::Send;
    a.bytes = _tx;
    a.n = n;
    return a;
}

Session::Action Session::poll(uint32_t now) {
    Action none;
    if (_waiting) {
        if ((uint32_t)(now - _sentAt) < kReplyTimeoutMs) return none;
        _waiting = false;
        if (++_misses >= kMaxMisses || _phase == Phase::Hello) {  // ignition off, wrong wiring, or asleep
            restart(now);
            _nextAt = now + 1000;
            return none;
        }
        _nextAt = now;
    }
    if ((int32_t)(now - _nextAt) < 0) return none;
    switch (_phase) {
        case Phase::Wake: {
            Action a;
            a.kind = Action::WakePulse;  // the caller drives TX low / high, then calls wakeDone()
            return a;
        }
        case Phase::Hello:
            if (!_helloSent) {
                _helloSent = true;
                _nextAt = now + kAfterWakeMs;
                return send(kWake, sizeof(kWake), now, false);
            }
            return send(kHello, sizeof(kHello), now, true);
        case Phase::Poll: {
            uint8_t req[5];
            tableRequest(_layout.table, req);
            return send(req, sizeof(req), now, true);
        }
    }
    return none;
}

void Session::wakeDone(uint32_t now) {
    _phase = Phase::Hello;
    _helloSent = false;
    _nextAt = now;
}

void Session::feed(const uint8_t* b, size_t n, vehicle::Snapshot& out, uint32_t now) {
    for (size_t i = 0; i < n; i++) {
        const uint8_t c = b[i];
        if (_echoLeft) {  // our own bytes coming back through the transceiver
            if (c == _tx[_echoPos]) {
                _echoPos++;
                _echoLeft--;
                continue;
            }
            _echoLeft = 0;  // an adapter without echo: this is already the reply
        }
        if (_rxLen >= sizeof(_rx)) _rxLen = 0;
        _rx[_rxLen++] = c;
        // [type][len]...: resynchronise on anything that cannot be a frame
        while (_rxLen >= 2 && (_rx[1] < 3 || _rx[1] > sizeof(_rx))) {
            memmove(_rx, _rx + 1, --_rxLen);
        }
        if (_rxLen >= 2 && _rxLen == _rx[1]) {
            if (checksum(_rx, _rxLen) == 0) {
                handleFrame(_rx, _rxLen, out, now);
                _rxLen = 0;
            } else {
                memmove(_rx, _rx + 1, --_rxLen);  // not a frame after all
            }
        }
    }
}

void Session::handleFrame(const uint8_t* f, size_t n, vehicle::Snapshot& out, uint32_t now) {
    if (!_waiting) return;
    if (_phase == Phase::Hello && n == 4 && f[2] == 0x00) {
        _phase = Phase::Poll;
        _waiting = false;
        _misses = 0;
        _nextAt = now + kPollGapMs;
        return;
    }
    if (_phase == Phase::Poll && n >= 5 && f[2] == 0x71 && f[3] == _layout.table) {
        const uint8_t* data = f + 4;
        const size_t len = n - 5;
        decode(_layout, data, len, out, now);
        size_t k = 0;
        for (size_t i = 0; i < len && k + 4 < sizeof(_dump); i++) k += (size_t)snprintf(_dump + k, sizeof(_dump) - k, "%02X ", data[i]);
        if (k) _dump[k - 1] = '\0';
        _frames++;
        _waiting = false;
        _misses = 0;
        _nextAt = now + kPollGapMs;
    }
}

const char* Session::status() const {
    switch (_phase) {
        case Phase::Wake: return _misses || _frames ? "NO ECU" : "K WAKE";
        case Phase::Hello: return "K INIT";
        case Phase::Poll: return "LIVE";
    }
    return "";
}

}  // namespace hondak
