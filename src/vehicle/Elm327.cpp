/**
 * @file Elm327.cpp
 * @brief ELM327 protocol (see Elm327.h)
 */

#include "Elm327.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace vehicle {

namespace {

const char* const kInit[] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATAT1", "ATSP0"};
constexpr uint8_t kInitSteps = sizeof(kInit) / sizeof(kInit[0]);

// polled in this order when supported; rpm in every other slot
const uint8_t kPids[] = {0x0D, 0x05, 0x0F, 0x11, 0x0B};
constexpr uint32_t kVoltEveryMs = 2000;

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** "41 0C 1A F8" / "410C1AF8" -> bytes. Returns the count, 0 if anything is not hex. */
size_t hexBytes(const char* s, uint8_t* out, size_t cap) {
    size_t n = 0;
    int hi = -1;
    for (; *s; s++) {
        if (*s == ' ') continue;
        const int v = hexVal(*s);
        if (v < 0) return 0;
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= cap) return 0;
            out[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return hi < 0 ? n : 0;
}

bool contains(const char* hay, const char* needle) { return strstr(hay, needle) != nullptr; }

}  // namespace

void Elm327::restart(uint32_t now) {
    _phase = Phase::Reset;
    _initStep = 0;
    _pollIndex = 0;
    _waiting = false;
    _retryAt = now;
    _timeouts = 0;
    _supported = 0;
    _lineLen = _replyLen = 0;
    _line[0] = _reply[0] = '\0';
}

bool Elm327::supports(uint8_t pid) const {
    if (pid == 0x0C) return true;  // asked anyway: every car that answers OBD-II reports rpm
    if (pid < 1 || pid > 0x20) return false;
    return (_supported >> (32 - pid)) & 1;
}

uint32_t Elm327::replyTimeout() const {
    if (_phase == Phase::Reset) return 2500;   // ATZ: the adapter reboots
    if (_phase == Phase::Search) return 12000; // "SEARCHING..." tries every protocol
    return 1500;
}

void Elm327::send(const char* cmd, uint32_t now) {
    snprintf(_cmd, sizeof(_cmd), "%s", cmd);
    _waiting = true;
    _sentAt = now;
    _replyLen = 0;
    _reply[0] = '\0';
}

const char* Elm327::poll(uint32_t now) {
    if (_waiting) {
        if ((uint32_t)(now - _sentAt) < replyTimeout()) return nullptr;
        _waiting = false;
        if (++_timeouts >= kMaxTimeouts) {  // adapter gone or stuck: init again
            const uint32_t t = _timeouts;
            restart(now);
            _timeouts = t;
            _retryAt = now + 1000;
            return nullptr;
        }
        _waiting = true;  // the same command once more
        _sentAt = now;
        _replyLen = 0;
        _reply[0] = '\0';
        return _cmd;
    }
    if ((int32_t)(now - _retryAt) < 0) return nullptr;
    switch (_phase) {
        case Phase::Reset:
        case Phase::Init:
            send(kInit[_initStep], now);
            return _cmd;
        case Phase::Search:
        case Phase::NoEcu:
            _phase = Phase::Search;
            send("0100", now);
            return _cmd;
        case Phase::Poll: {
            if ((uint32_t)(now - _lastVolt) >= kVoltEveryMs) {
                _lastVolt = now;
                send("ATRV", now);
                return _cmd;
            }
            uint8_t pid = 0x0C;
            if (_pollIndex & 1) {  // odd slots: the next supported one of the others
                for (size_t tries = 0; tries < sizeof(kPids); tries++) {
                    const uint8_t p = kPids[(_pollIndex / 2 + tries) % sizeof(kPids)];
                    if (supports(p)) {
                        pid = p;
                        break;
                    }
                }
            }
            _pollIndex++;
            char c[8];
            snprintf(c, sizeof(c), "01%02X", pid);
            send(c, now);
            return _cmd;
        }
    }
    return nullptr;
}

void Elm327::feed(const char* data, size_t n, Snapshot& out, uint32_t now) {
    for (size_t i = 0; i < n; i++) {
        const char c = data[i];
        if (c == '\r' || c == '\n' || c == '>') {
            if (_lineLen) {  // one line of the reply
                _line[_lineLen] = '\0';
                const size_t room = sizeof(_reply) - 1 - _replyLen;
                const size_t take = _lineLen + 1 <= room ? _lineLen : 0;
                if (take) {
                    memcpy(_reply + _replyLen, _line, take);
                    _replyLen += take;
                    _reply[_replyLen++] = '\n';
                    _reply[_replyLen] = '\0';
                }
                _lineLen = 0;
            }
            if (c == '>' && _waiting) {
                _waiting = false;
                _timeouts = 0;
                handleReply(out, now);
            }
            continue;
        }
        if (c == 0 || _lineLen + 1 >= sizeof(_line)) continue;  // NULs some clones send; overlong lines are cut
        _line[_lineLen++] = c;
    }
}

bool Elm327::decodeMode01(const char* line, Snapshot& out, uint32_t now) {
    uint8_t b[8];
    const size_t n = hexBytes(line, b, sizeof(b));
    if (n < 3 || b[0] != 0x41) return false;
    switch (b[1]) {
        case 0x0C:
            if (n < 4) return false;
            out.set(kRpm, (b[2] * 256 + b[3]) / 4.0f, now);
            return true;
        case 0x0D: out.set(kSpeed, b[2], now); return true;
        case 0x05: out.set(kCoolant, b[2] - 40.0f, now); return true;
        case 0x0F: out.set(kIntake, b[2] - 40.0f, now); return true;
        case 0x11: out.set(kThrottle, b[2] * 100.0f / 255.0f, now); return true;
        case 0x0B: out.set(kMap, b[2], now); return true;
        default: return false;
    }
}

bool Elm327::parseVoltage(const char* line, float& volts) {
    char* end = nullptr;
    const float v = strtof(line, &end);
    if (end == line || v < 3 || v > 40) return false;
    while (*end == ' ') end++;
    if (*end != 'V' && *end != 'v' && *end != '\0' && *end != '\n') return false;
    volts = v;
    return true;
}

void Elm327::handleReply(Snapshot& out, uint32_t now) {
    switch (_phase) {
        case Phase::Reset:
            if (contains(_reply, "ELM") || contains(_reply, "STN") || contains(_reply, "OBD")) {
                const char* id = strstr(_reply, "ELM");
                if (!id) id = strstr(_reply, "STN");
                if (!id) id = _reply;
                size_t k = 0;
                while (id[k] && id[k] != '\n' && k + 1 < sizeof(_adapter)) {
                    _adapter[k] = id[k];
                    k++;
                }
                _adapter[k] = '\0';
            }
            _phase = Phase::Init;
            _initStep = 1;
            _retryAt = now + 300;  // let it settle after the reset
            return;
        case Phase::Init:
            // "OK" expected; a clone that answers "?" to an optional setting is fine too
            if (++_initStep >= kInitSteps) {
                _phase = Phase::Search;
                _retryAt = now;
            }
            return;
        case Phase::Search:
        case Phase::NoEcu: {
            // "SEARCHING...\n4100BE3EB811" or "UNABLE TO CONNECT" / "NO DATA" / "BUS INIT: ...ERROR"
            for (char* line = _reply; *line;) {
                char* nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                uint8_t b[8];
                const size_t n = hexBytes(line, b, sizeof(b));
                if (n >= 6 && b[0] == 0x41 && b[1] == 0x00) {
                    _supported = (uint32_t)b[2] << 24 | (uint32_t)b[3] << 16 | (uint32_t)b[4] << 8 | b[5];
                    _phase = Phase::Poll;
                    _pollIndex = 0;
                    _lastVolt = now - kVoltEveryMs;
                    return;
                }
                if (!nl) break;
                line = nl + 1;
            }
            _phase = Phase::NoEcu;  // ignition off / no OBD-II: ask again a little later
            _retryAt = now + 4000;
            return;
        }
        case Phase::Poll: {
            if (!strcmp(_cmd, "ATRV")) {
                float v;
                if (parseVoltage(_reply, v)) out.set(kBattery, v, now);
                return;
            }
            bool any = false;
            for (char* line = _reply; *line;) {
                char* nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                any |= decodeMode01(line, out, now);  // several ECUs may answer: each line on its own
                if (!nl) break;
                line = nl + 1;
            }
            if (!any && (contains(_reply, "UNABLE") || contains(_reply, "BUS ERROR") || contains(_reply, "CAN ERROR") ||
                         contains(_reply, "STOPPED"))) {
                _phase = Phase::NoEcu;  // engine switched off, or the bus went away
                _retryAt = now + 2000;
            }
            return;
        }
    }
}

const char* Elm327::status() const {
    switch (_phase) {
        case Phase::Reset: return _timeouts ? "NO ADAPTER" : "ELM RESET";
        case Phase::Init: return "ELM INIT";
        case Phase::Search: return "ECU SEARCH";
        case Phase::NoEcu: return "NO ECU";
        case Phase::Poll: return "LIVE";
    }
    return "";
}

}  // namespace vehicle
