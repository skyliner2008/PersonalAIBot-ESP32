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

const char* const kInit[] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATAT1"};  // then the protocol (ATSP0 / ATSPA<p>)
constexpr uint8_t kInitFixed = sizeof(kInit) / sizeof(kInit[0]);

constexpr uint32_t kVoltEveryMs = 2000;
constexpr uint32_t kVoltEngineOffMs = 500;  // ignition on, engine off: the next start's voltage dip is worth catching
constexpr uint32_t kVoltNoEcuMs = 10000;    // parked (no ECU answers): the battery still reads at the socket

// the auto search failed twice: each protocol in turn, the most common first (CAN 11 bit 500k: most cars since 2008)
const uint8_t kProtoOrder[] = {6, 8, 7, 9, 5, 4, 3, 1, 2};
constexpr uint8_t kProtoCount = sizeof(kProtoOrder);
// what a freeze frame is read for (when the car has the PID)
const uint8_t kFreezePids[] = {0x0C, 0x0D, 0x05, 0x04, 0x06, 0x07, 0x0B, 0x11};

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

/** The bus is not there (ignition off, no OBD-II, wrong protocol). */
bool busGone(const char* r) {
    return contains(r, "UNABLE") || contains(r, "BUS ERROR") || contains(r, "CAN ERROR") || contains(r, "BUS INIT") ||
           contains(r, "STOPPED");
}

/** Calls fn(bytes, n) for each line of the reply that is hex. */
template <class Fn>
void eachLine(char* reply, Fn fn) {
    for (char* line = reply; *line;) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        uint8_t b[16];
        const size_t n = hexBytes(line, b, sizeof(b));
        if (n) fn(b, n, line);
        if (nl) *nl = '\n';
        if (!nl) break;
        line = nl + 1;
    }
}

}  // namespace

void Elm327::restart(uint32_t now) {
    _phase = Phase::Reset;
    _initStep = 0;
    _pollIndex = 0;
    _waiting = false;
    _answered = false;
    _retryAt = now;
    _timeouts = 0;
    _searchFails = 0;
    _tryProto = 0;
    _protoPending = false;
    _noEcuSince = 0;
    memset(_supported, 0, sizeof(_supported));
    memset(_lastAt, 0, sizeof(_lastAt));
    memset(_gap, 0, sizeof(_gap));
    memset(_miss, 0, sizeof(_miss));
    _droppedBits = 0;
    _emptyRun = 0;
    _lastDataAt = now;
    _rpm = -1;
    _atrvBad = 0;
    _scanWanted = _clearWanted = false;
    _codesAt = now;
    _nQueue = 0;
    _job = kJobNone;
    _jobIdx = -1;
    _resendAt = 0;
    _pendingTries = 0;
    const uint32_t seq = _diag.scanSeq;
    _diag = obd::Diag();
    _diag.available = true;
    _diag.scanSeq = seq;  // keeps counting: a scan someone waits for must not look done already
    changed();
    _lineLen = _replyLen = 0;
    _line[0] = _reply[0] = '\0';
}

bool Elm327::responsePending() {
    bool pending = false, positive = false;
    eachLine(_reply, [&](const uint8_t* b, size_t n, const char*) {
        if (n >= 3 && b[0] == 0x7F && b[2] == 0x78) pending = true;
        else if (n >= 1 && b[0] != 0x7F) positive = true;  // another ECU (or the frame count) answered
    });
    return pending && !positive;
}

bool Elm327::supports(uint8_t pid) const {
    if (pid == 0x0C) return true;  // asked anyway: every car that answers OBD-II reports rpm
    if (pid < 1 || pid > 0xE0) return false;
    return (_supported[(pid - 1) / 32] >> (31 - (pid - 1) % 32)) & 1;
}

bool Elm327::dropped(uint8_t pid) const {
    size_t n;
    const obd::Pid* t = obd::pids(n);
    for (size_t i = 0; i < n && i < kTable; i++) {
        if (t[i].pid == pid) return (_droppedBits >> i) & 1;
    }
    return false;
}

uint32_t Elm327::replyTimeout() const {
    if (_phase == Phase::Reset) return 2500;  // ATZ: the adapter reboots
    switch (_job) {
        case kJobSearch: return 12000;  // "SEARCHING..." tries every protocol (ISO 9141 / KWP: seconds each)
        case kJobDpn: case kJobPids: case kJobVin: case kJobCodes: case kJobPending: case kJobPermanent:
        case kJobFreezeDtc: case kJobFreeze: case kJobClear:
            return 5000;  // several ECUs, multi-frame, or a slow K-Line bus
        default: return 1500;
    }
}

void Elm327::send(const char* cmd, uint8_t job, uint32_t now) {
    snprintf(_cmd, sizeof(_cmd), "%s", cmd);
    _job = job;
    _waiting = true;
    _sentAt = now;
    _replyLen = 0;
    _reply[0] = '\0';
    if (_trace) _trace->sent(now, _cmd);
}

void Elm327::push(uint8_t job, uint8_t arg, bool front) {
    if (_nQueue >= sizeof(_queue) / sizeof(_queue[0])) return;
    if (front) {
        memmove(_queue + 1, _queue, _nQueue * sizeof(Queued));
        _queue[0] = {job, arg};
    } else {
        _queue[_nQueue] = {job, arg};
    }
    _nQueue++;
}

// what the car has (ATDPN, then 0120 / 0140 ... while the previous range says the next one exists)
void Elm327::queueDiscovery() {
    push(kJobDpn);
    for (uint8_t base = 0x20; base <= 0xC0; base += 0x20) push(kJobPids, base);
}

// one code scan: light + count, stored / pending / permanent codes, the freeze frame; kJobDone publishes it
void Elm327::queueCodeScan() {
    _scanStored.clear();
    _scanPending.clear();
    _scanPermanent.clear();
    _scanFreeze = obd::FreezeFrame();
    push(kJobMonitor);
    push(kJobCodes);
    push(kJobPending);
    push(kJobPermanent);
    push(kJobFreezeDtc);
    push(kJobDone);
}

bool Elm327::jobApplies(const Queued& q) const {
    switch (q.job) {
        case kJobPids: return supports(q.arg);  // bit 0x20 of 0100 = "PIDs 21-40 exist", and so on
        case kJobFuel: return supports(0x51);
        case kJobMonitor: return supports(0x01);
        case kJobFreeze: return supports(q.arg);
        default: return true;
    }
}

const char* Elm327::nextJob(uint32_t now) {
    while (_nQueue) {
        const Queued q = _queue[0];
        memmove(_queue, _queue + 1, (size_t)(_nQueue - 1) * sizeof(Queued));
        _nQueue--;
        if (q.job == kJobDone) {  // the scan is complete: publish its results together
            _diag.stored = _scanStored;
            _diag.pending = _scanPending;
            _diag.permanent = _scanPermanent;
            _diag.freeze = _scanFreeze;
            _diag.codesValid = true;
            _diag.codesAt = now;
            _diag.scanSeq++;
            _codesAt = now;
            changed();
            continue;
        }
        if (!jobApplies(q)) continue;
        char c[12];
        switch (q.job) {
            case kJobDpn: snprintf(c, sizeof(c), "ATDPN"); break;
            case kJobPids: snprintf(c, sizeof(c), "01%02X", q.arg); break;
            case kJobFuel: snprintf(c, sizeof(c), "0151"); break;
            case kJobMonitor: snprintf(c, sizeof(c), "0101"); break;
            case kJobVin: snprintf(c, sizeof(c), "0902"); break;
            case kJobCodes: snprintf(c, sizeof(c), "03"); break;
            case kJobPending: snprintf(c, sizeof(c), "07"); break;
            case kJobPermanent: snprintf(c, sizeof(c), "0A"); break;
            case kJobFreezeDtc: snprintf(c, sizeof(c), "020200"); break;
            case kJobFreeze: snprintf(c, sizeof(c), "02%02X00", q.arg); break;
            case kJobClear: snprintf(c, sizeof(c), "04"); break;
            default: continue;
        }
        send(c, q.job, now);
        _jobArg = q.arg;
        return _cmd;
    }
    return nullptr;
}

int Elm327::pickPid(uint32_t now) const {
    size_t n;
    const obd::Pid* t = obd::pids(n);
    int best = -1;
    float bestScore = 0.999f;  // only what is due
    for (size_t i = 0; i < n && i < kTable; i++) {
        const obd::Pid& p = t[i];
        if (!supports(p.pid) || ((_droppedBits >> i) & 1)) continue;
        if ((p.flags & obd::kPidBatteryFallback) && _atrvBad < 2) continue;
        if (i == 0 && _focus == Focus::Gauge) continue;  // rpm has its own slots
        uint32_t period = _focus == Focus::Background ? p.backMs : p.gaugeMs;
        if (!period) period = 500;  // rpm on the info page
        // never asked yet: first, in table order (the dash fills up in a second)
        const float score = !_lastAt[i] ? 1000.0f - (float)i : (float)(uint32_t)(now - _lastAt[i]) / (float)period;
        if (score > bestScore) {
            bestScore = score;
            best = (int)i;
        }
    }
    return best;
}

const char* Elm327::sendPid(int idx, uint32_t now) {
    size_t n;
    const obd::Pid* t = obd::pids(n);
    if (_lastAt[idx]) _gap[idx] = now - _lastAt[idx];
    _lastAt[idx] = now;
    char c[8];
    snprintf(c, sizeof(c), "01%02X", t[idx].pid);
    send(c, kJobPid, now);
    _jobIdx = (int8_t)idx;
    _jobArg = t[idx].pid;
    return _cmd;
}

const char* Elm327::poll(uint32_t now) {
    if (_waiting) {
        if ((uint32_t)(now - _sentAt) < replyTimeout()) return nullptr;
        _waiting = false;
        if (_trace) _trace->timedOut(now, _cmd);
        if (++_timeouts >= kMaxTimeouts) {  // adapter gone or stuck: init again
            const uint32_t t = _timeouts;
            const bool silent = !_answered;  // never a prompt since the restart: the baud rate may be wrong
            restart(now);
            _timeouts = t;
            if (silent) _silentResets++;
            _retryAt = now + 1000;
            return nullptr;
        }
        switch (_job) {  // a scan step the car does not answer (an old ECU, 0902 / 0A): skipped, not sent again
            case kJobClear:
                _diag.clearResult = obd::kClearFailed;
                changed();
                [[fallthrough]];
            case kJobDpn: case kJobPids: case kJobFuel: case kJobMonitor: case kJobVin: case kJobCodes:
            case kJobPending: case kJobPermanent: case kJobFreezeDtc: case kJobFreeze:
                _job = kJobNone;
                return nullptr;
            default:
                break;
        }
        _waiting = true;  // the same command once more
        _sentAt = now;
        _replyLen = 0;
        _reply[0] = '\0';
        if (_trace) _trace->sent(now, _cmd);
        return _cmd;
    }
    if (_resendAt) {  // the ECU said "response pending": the same request again (job and PID kept)
        if ((int32_t)(now - _resendAt) < 0) return nullptr;
        _resendAt = 0;
        _waiting = true;
        _sentAt = now;
        _replyLen = 0;
        _reply[0] = '\0';
        if (_trace) _trace->sent(now, _cmd);
        return _cmd;
    }
    if ((int32_t)(now - _retryAt) < 0) return nullptr;
    char c[12];
    switch (_phase) {
        case Phase::Reset:
        case Phase::Init:
            if (_initStep < kInitFixed) {
                send(kInit[_initStep], kJobNone, now);
            } else {  // the protocol: automatic, trying the one that worked last time first
                if (_hint >= 1 && _hint <= 9) snprintf(c, sizeof(c), "ATSPA%X", _hint);
                else snprintf(c, sizeof(c), "ATSP0");
                send(c, kJobNone, now);
            }
            return _cmd;
        case Phase::NoEcu:
            if ((uint32_t)(now - _lastVolt) >= kVoltNoEcuMs && _atrvBad < 2) {
                _lastVolt = now;
                send("ATRV", kJobVolt, now);
                return _cmd;
            }
            _phase = Phase::Search;
            [[fallthrough]];
        case Phase::Search:
            if (_protoPending) {
                const uint8_t p = _tryProto >= 1 && _tryProto <= kProtoCount ? kProtoOrder[_tryProto - 1] : 0;
                if (!p) _tryProto = 0;  // the whole list failed too: the automatic search again
                if (p) snprintf(c, sizeof(c), "ATSP%X", p);
                else if (_hint >= 1 && _hint <= 9) snprintf(c, sizeof(c), "ATSPA%X", _hint);
                else snprintf(c, sizeof(c), "ATSP0");
                send(c, kJobProto, now);
                return _cmd;
            }
            send("0100", kJobSearch, now);
            return _cmd;
        case Phase::Discover:
        case Phase::Poll:
            break;
    }
    if (_phase == Phase::Poll && _atrvBad < 2) {
        const bool engineOff = _rpm >= 0 && _rpm < 300 && (uint32_t)(now - _rpmAt) < 3000;
        if ((uint32_t)(now - _lastVolt) >= (engineOff ? kVoltEngineOffMs : kVoltEveryMs)) {
            _lastVolt = now;
            send("ATRV", kJobVolt, now);
            return _cmd;
        }
    }
    if (_phase == Phase::Poll && !_nQueue) {
        if (_clearWanted) {
            _clearWanted = _scanWanted = false;
            push(kJobClear);
            queueCodeScan();
        } else if (_scanWanted || (_diag.codesValid && (uint32_t)(now - _codesAt) >= kCodesEveryMs)) {
            _scanWanted = false;
            queueCodeScan();
        }
    }
    // a gauge on screen: rpm every other request; jobs (codes, VIN) take the other slots until they are done
    const bool rpmSlot = _phase == Phase::Poll && _focus == Focus::Gauge && (_pollIndex & 1);
    _pollIndex++;
    if (!rpmSlot) {
        if (const char* job = nextJob(now)) return job;
        if (_phase == Phase::Discover) {  // the PID ranges are known: live data, the rest in between
            _phase = Phase::Poll;
            push(kJobFuel);
            push(kJobVin);
            queueCodeScan();
            changed();
        }
    }
    int idx = rpmSlot ? 0 : pickPid(now);  // index 0 of the table = rpm
    if (idx < 0 && _focus == Focus::Gauge) idx = 0;
    if (idx < 0) {  // nothing due: a quiet bus while nobody looks
        _retryAt = now + 25;
        return nullptr;
    }
    return sendPid(idx, now);
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
                _answered = true;
                _timeouts = 0;
                if (_trace) _trace->replied(now, _reply);
                // T117: "7F 09 78" = response pending: the ECU answers later, but a clone gives the prompt at once and
                // the real answer is lost (VIN in the user's car; the next two requests then got NO DATA) - ask again
                // after a pause instead of taking it as "not supported"
                if (responsePending() && _pendingTries < kPendingTries) {
                    _pendingTries++;
                    _resendAt = (now + kPendingWaitMs) | 1;
                    continue;
                }
                _pendingTries = 0;
                handleReply(out, now);
            }
            continue;
        }
        if (c == 0 || _lineLen + 1 >= sizeof(_line)) continue;  // NULs some clones send; overlong lines are cut
        _line[_lineLen++] = c;
    }
}

bool Elm327::decodeMode01(const char* line, Snapshot& out, uint32_t now, uint32_t ttlMs) {
    uint8_t b[8];
    const size_t n = hexBytes(line, b, sizeof(b));
    if (n < 3 || b[0] != 0x41) return false;
    const obd::Pid* p = obd::findPid(b[1]);
    if (!p || p->ch >= kChannelCount || n < 2u + p->bytes) return false;
    float v;
    if (!obd::decodePid(b[1], b + 2, n - 2, v)) return false;
    out.set(p->ch, v, now, ttlMs);
    return true;
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

void Elm327::goNoEcu(uint32_t now) {
    if (!_noEcuSince) _noEcuSince = now;
    _phase = Phase::NoEcu;
    _nQueue = 0;
    // retry often at first (the engine is being started), then calmly (parked: the adapter stays powered)
    _retryAt = now + ((uint32_t)(now - _noEcuSince) < 60000 ? 4000 : 15000);
    _lastVolt = now - kVoltNoEcuMs + 1500;  // the parked battery voltage soon
    changed();
}

void Elm327::handleSearch(uint32_t now) {
    // "SEARCHING...\n4100BE3EB811" (one line per ECU) or "UNABLE TO CONNECT" / "NO DATA" / "BUS INIT: ...ERROR"
    bool found = false;
    eachLine(_reply, [&](const uint8_t* b, size_t n, const char*) {
        if (n >= 6 && b[0] == 0x41 && b[1] == 0x00) {
            _supported[0] |= (uint32_t)b[2] << 24 | (uint32_t)b[3] << 16 | (uint32_t)b[4] << 8 | b[5];
            found = true;
        }
    });
    if (found) {
        // back from NO ECU: what went silent while the car switched off is asked again
        memset(_miss, 0, sizeof(_miss));
        _droppedBits = 0;
        _phase = Phase::Discover;
        _pollIndex = 0;
        _lastVolt = now - kVoltEveryMs;
        _searchFails = 0;
        _tryProto = 0;
        _noEcuSince = 0;
        _lastDataAt = now;
        _emptyRun = 0;
        queueDiscovery();
        changed();
        return;
    }
    // ignition off / no OBD-II / a protocol the clone's search misses: next time a protocol of the list
    if (_tryProto) {
        _tryProto++;
        _protoPending = true;
    } else if (++_searchFails >= 2) {
        _searchFails = 0;
        _tryProto = 1;
        _protoPending = true;
    }
    goNoEcu(now);
}

bool Elm327::handlePid(Snapshot& out, uint32_t now) {
    size_t n;
    const obd::Pid* t = obd::pids(n);
    if (_jobIdx < 0 || (size_t)_jobIdx >= n) return false;
    const obd::Pid& p = t[_jobIdx];
    uint32_t period = _focus == Focus::Background ? p.backMs : p.gaugeMs;
    if (!period) period = 500;
    // fresh for 3 x the real time between two readings: a slow bus shows older values, not "--"
    uint32_t ttl = 3 * (_gap[_jobIdx] > period ? _gap[_jobIdx] : period);
    if (ttl < kStaleMs) ttl = kStaleMs;
    if (ttl > 180000) ttl = 180000;
    bool any = false;
    if (p.pid == 0x01) {  // monitor status: one line per ECU (light on if any says so, codes add up)
        obd::Readiness r;
        bool mil = false;
        unsigned count = 0;
        eachLine(_reply, [&](const uint8_t* b, size_t len, const char*) {
            if (len < 6 || b[0] != 0x41 || b[1] != 0x01) return;
            const obd::Readiness e = obd::decodeMonitor(b + 2);
            mil |= e.mil;
            count += e.count;
            if (!r.valid || e.total > r.total) r = e;  // the readiness of the engine ECU (it has the most monitors)
            any = true;
        });
        if (any) {
            r.mil = mil;
            r.count = (uint8_t)(count > 127 ? 127 : count);
            // polled (not part of a scan): the count or the light changed since the codes were read - read them again
            if (_job == kJobPid && _diag.codesValid && (r.mil != _diag.ready.mil || r.count != _diag.ready.count)) {
                _scanWanted = true;
            }
            _diag.ready = r;
            changed();
        }
    } else {
        eachLine(_reply, [&](const uint8_t*, size_t, const char* line) { any |= decodeMode01(line, out, now, ttl); });
        if (any && p.pid == 0x0C) {
            _rpm = out.value(kRpm);
            _rpmAt = now;
        }
    }
    if (any) {
        _miss[_jobIdx] = 0;
        _emptyRun = 0;
        _lastDataAt = now;
        return true;
    }
    if (++_miss[_jobIdx] >= kMaxMisses && p.pid != 0x0C) _droppedBits |= 1u << _jobIdx;  // not on this car after all
    if (_emptyRun < 255) _emptyRun++;
    return false;
}

void Elm327::handleJob(Snapshot& out, uint32_t now) {
    (void)out;
    switch (_job) {
        case kJobDpn: {
            const uint8_t p = obd::parseProtocol(_reply);
            if (p && p != _diag.protocol) {
                _diag.protocol = p;
                changed();
            }
            return;
        }
        case kJobPids:
            eachLine(_reply, [&](const uint8_t* b, size_t n, const char*) {
                if (n < 6 || b[0] != 0x41 || b[1] != _jobArg) return;
                _supported[_jobArg / 32] |= (uint32_t)b[2] << 24 | (uint32_t)b[3] << 16 | (uint32_t)b[4] << 8 | b[5];
            });
            {
                uint8_t count = 0;
                for (int pid = 1; pid <= 0xE0; pid++) {
                    if (pid % 32 && supports((uint8_t)pid)) count++;  // not the "next range exists" bits
                }
                _diag.supportedPids = count;
                changed();
            }
            return;
        case kJobFuel:
            eachLine(_reply, [&](const uint8_t* b, size_t n, const char*) {
                if (n >= 3 && b[0] == 0x41 && b[1] == 0x51 && !_diag.fuelType) _diag.fuelType = b[2];
            });
            changed();
            return;
        case kJobMonitor: {
            // the same as the polled PID 01
            const int8_t saved = _jobIdx;
            size_t n;
            const obd::Pid* t = obd::pids(n);
            for (size_t i = 0; i < n; i++) {
                if (t[i].pid == 0x01) _jobIdx = (int8_t)i;
            }
            handlePid(out, now);
            _jobIdx = saved;
            return;
        }
        case kJobVin: {
            obd::Msg m[6];
            const size_t n = obd::messages(_reply, m, 6);
            char vin[18];
            if (obd::parseVin(m, n, vin)) {
                memcpy(_diag.vin, vin, sizeof(vin));
                changed();
            }
            return;
        }
        case kJobCodes:
        case kJobPending:
        case kJobPermanent: {
            obd::Msg m[6];
            const size_t n = obd::messages(_reply, m, 6);
            const uint8_t service = _job == kJobCodes ? 0x43 : _job == kJobPending ? 0x47 : 0x4A;
            obd::DtcList& list = _job == kJobCodes ? _scanStored : _job == kJobPending ? _scanPending : _scanPermanent;
            obd::parseDtcReply(m, n, service, _diag.protocol, list);  // NO DATA: none (or not supported)
            return;
        }
        case kJobFreezeDtc:
            eachLine(_reply, [&](const uint8_t* b, size_t n, const char*) {
                if (n < 5 || b[0] != 0x42 || b[1] != 0x02 || _scanFreeze.dtc) return;
                _scanFreeze.dtc = (uint16_t)(b[3] << 8 | b[4]);
            });
            if (_scanFreeze.dtc) {  // read the frame itself before the scan is published
                for (int i = (int)sizeof(kFreezePids) - 1; i >= 0; i--) push(kJobFreeze, kFreezePids[i], true);
            }
            return;
        case kJobFreeze:
            eachLine(_reply, [&](const uint8_t* b, size_t n, const char*) {
                const obd::Pid* p = obd::findPid(_jobArg);
                if (n < 4 || b[0] != 0x42 || b[1] != _jobArg || !p || _scanFreeze.n >= 8) return;
                float v;
                if (n >= 3u + p->bytes && obd::decodePid(_jobArg, b + 3, n - 3, v)) {
                    for (uint8_t k = 0; k < _scanFreeze.n; k++) {
                        if (_scanFreeze.pid[k] == _jobArg) return;  // a second ECU's copy
                    }
                    _scanFreeze.pid[_scanFreeze.n] = _jobArg;
                    _scanFreeze.value[_scanFreeze.n++] = v;
                }
            });
            return;
        case kJobClear: {
            bool ok = false, refused = false;
            eachLine(_reply, [&](const uint8_t* b, size_t n, const char*) {
                if (b[0] == 0x44) ok = true;
                if (n >= 2 && b[0] == 0x7F && b[1] == 0x04) refused = true;  // 7F 04 22: conditions not correct
            });
            _diag.clearResult = ok ? obd::kClearOk : refused ? obd::kClearRefused : obd::kClearFailed;
            changed();
            _retryAt = now + 1500;  // the ECUs reset their monitors: a moment before the codes are read again
            return;
        }
        default:
            return;
    }
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
            if (++_initStep > kInitFixed) {
                _phase = Phase::Search;
                _retryAt = now;
            }
            return;
        case Phase::Search:
        case Phase::NoEcu:
            if (_job == kJobVolt) {
                float v;
                if (parseVoltage(_reply, v)) out.set(kBattery, v, now, 25000);  // read every 10 s while parked
                else if (_atrvBad < 255) _atrvBad++;
                return;
            }
            if (_job == kJobProto) {  // ATSPn: the next 0100 tries that protocol
                _protoPending = false;
                return;
            }
            handleSearch(now);
            return;
        case Phase::Discover:
        case Phase::Poll:
            if (_job == kJobVolt) {
                float v;
                if (parseVoltage(_reply, v)) {
                    out.set(kBattery, v, now, 6000);
                    _atrvBad = 0;
                } else if (_atrvBad < 255) {
                    _atrvBad++;  // twice: PID 42 (module voltage) is polled instead
                }
                return;
            }
            if (_job == kJobPid) {
                if (!handlePid(out, now)) {
                    // engine switched off, or the bus went away: errors at once, or a run of empty replies
                    if (busGone(_reply) || (_emptyRun >= 8 && (uint32_t)(now - _lastDataAt) > 5000)) goNoEcu(now);
                }
                return;
            }
            handleJob(out, now);
            return;
    }
}

const char* Elm327::status() const {
    switch (_phase) {
        case Phase::Reset: return _timeouts ? "NO ADAPTER" : "ELM RESET";
        case Phase::Init: return "ELM INIT";
        case Phase::Search: return "ECU SEARCH";
        case Phase::Discover: return "ECU SCAN";
        case Phase::NoEcu: return "NO ECU";
        case Phase::Poll: return "LIVE";
    }
    return "";
}

}  // namespace vehicle
