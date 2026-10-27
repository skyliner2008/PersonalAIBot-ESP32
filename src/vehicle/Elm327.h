/**
 * @file Elm327.h
 * @brief OBD-II through an ELM327 (or STN / clone) adapter: init, protocol search, what the car supports,
 *        trouble codes / VIN / freeze frame, and live PID polling by priority - independent of the link
 *        (BLE or UART), host tested
 * @see wiki/16-Vehicle-Dash.md
 *
 * The adapter is a text device: a command ends in '\r', the reply ends with the '>' prompt. Sequence:
 *   ATZ / ATE0 (no echo) / ATL0 / ATS0 (no spaces) / ATH0 (no headers) / ATAT1 / ATSP0 or ATSPA<p> (search,
 *   trying the protocol that worked last time first) -> "0100" (finds the car: SEARCHING... 41 00 ...)
 *   -> ATDPN (which protocol) + 0120 / 0140 ... 01C0 (every PID the car has, T112) -> live polling, with
 *   0101 (check-engine light), 0151 (fuel), 0902 (VIN), 03 / 07 / 0A (stored / pending / permanent codes) and
 *   02 (freeze frame) worked in between. The auto search failing twice -> each protocol in turn (6 8 7 9 5 4 3 1 2):
 *   some clones cannot find ISO 9141 / KWP by themselves.
 *
 * Polling (T112): each PID has a wanted refresh (obd::Pid) for a dash page and for "dash closed"; the most
 * overdue one is asked next, rpm every other request while a gauge shows it. A value stays fresh 3 x the time
 * it really took between two readings of it, so a slow bus shows older values instead of "--". A PID that
 * answers NO DATA three times is dropped; the bus answering nothing for a while = ignition off (NO ECU).
 * Codes are read at connect, every 5 minutes, when 0101 says their count changed, and on request.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Obd.h"
#include "VehicleData.h"

namespace vehicle {

class Elm327 {
public:
    enum class Phase : uint8_t { Reset, Init, Search, Discover, Poll, NoEcu };
    /** What the readings are for right now: a gauge (rpm fast), the info page, or nobody looking. */
    enum class Focus : uint8_t { Background, Gauge, Info };

    /** Sees every command, reply and timeout - the SD card log (T115). */
    class Trace {
    public:
        virtual ~Trace() = default;
        virtual void sent(uint32_t now, const char* cmd) = 0;
        virtual void replied(uint32_t now, const char* reply) = 0;  // its lines, '\n'-separated
        virtual void timedOut(uint32_t now, const char* cmd) = 0;
    };
    void setTrace(Trace* t) { _trace = t; }

    /** Start over (new link, or after errors). */
    void restart(uint32_t now);
    /** The protocol that worked last time (1-9, saved by the caller): tried first. 0 = plain auto search. */
    void setProtocolHint(uint8_t p) { _hint = p <= 12 ? p : 0; }
    void setFocus(Focus f) { _focus = f; }

    /**
     * The command to send now (without '\r'), or nullptr while a reply is awaited / nothing is due. A command
     * that got no prompt in time is sent again; after kMaxTimeouts the init starts over.
     */
    const char* poll(uint32_t now);

    /** Bytes from the adapter. Complete replies update `out` (and diag()). */
    void feed(const char* data, size_t n, Snapshot& out, uint32_t now);

    /** Read the trouble codes again soon (the AI / the menu asked). */
    void requestScan() { _scanWanted = true; }
    /** Clear the codes (mode 04 - engine off, ignition on), then read them again. */
    void requestClear() { _clearWanted = true; }

    Phase phase() const { return _phase; }
    /** Short status for the dash ("ELM INIT", "ECU SEARCH", "ECU SCAN", "NO ECU", "LIVE"). */
    const char* status() const;
    /** Adapter id from ATZ ("ELM327 v1.5"). */
    const char* adapter() const { return _adapter; }
    uint32_t timeouts() const { return _timeouts; }
    bool supports(uint8_t pid) const;
    /** The PID answered NO DATA too often and is not asked any more (until the next connect). */
    bool dropped(uint8_t pid) const;
    const obd::Diag& diag() const { return _diag; }
    /** +1 whenever diag() changed. */
    uint32_t diagVersion() const { return _diagVer; }
    /** The adapter answered since restart() (the baud rate is right). */
    bool answered() const { return _answered; }
    /** How often the init started over because the adapter never answered (wrong baud rate?). */
    uint32_t silentResets() const { return _silentResets; }

    /** Decode one reply line of mode 01 ("410C1AF8", spaces allowed) into `out`. False if it is not one. */
    static bool decodeMode01(const char* line, Snapshot& out, uint32_t now, uint32_t ttlMs = kStaleMs);
    /** "12.6V" -> 12.6 (ATRV). */
    static bool parseVoltage(const char* line, float& volts);

    static constexpr uint32_t kMaxTimeouts = 3;
    static constexpr uint32_t kCodesEveryMs = 5 * 60 * 1000;
    static constexpr uint8_t kMaxMisses = 3;  // NO DATA in a row -> the PID is dropped
    static constexpr size_t kTable = 24;      // >= obd::pids() entries

private:
    enum Job : uint8_t {
        kJobNone, kJobProto, kJobDpn, kJobPids, kJobFuel, kJobMonitor, kJobVin, kJobCodes, kJobPending,
        kJobPermanent, kJobFreezeDtc, kJobFreeze, kJobClear, kJobDone, kJobVolt, kJobPid, kJobSearch
    };
    struct Queued {
        uint8_t job, arg;
    };

    Trace* _trace = nullptr;
    Phase _phase = Phase::Reset;
    Focus _focus = Focus::Gauge;
    uint8_t _initStep = 0;
    uint8_t _pollIndex = 0;
    bool _waiting = false, _answered = false;
    uint32_t _sentAt = 0, _retryAt = 0, _lastVolt = 0;
    uint32_t _timeouts = 0, _silentResets = 0;
    uint8_t _hint = 0;
    uint8_t _searchFails = 0, _tryProto = 0;
    bool _protoPending = false;  // an explicit ATSPn goes out before the next 0100
    uint32_t _noEcuSince = 0;
    uint32_t _supported[7] = {};  // PIDs 0x01-0xE0 (bit 31 of word k = PID k*32+1, as the car sends them)
    // per PID of obd::pids(): last request, the time between the last two, NO DATA count
    uint32_t _lastAt[kTable] = {};
    uint32_t _gap[kTable] = {};
    uint8_t _miss[kTable] = {};
    uint32_t _droppedBits = 0;
    uint8_t _emptyRun = 0;
    uint32_t _lastDataAt = 0;
    float _rpm = -1;
    uint32_t _rpmAt = 0;
    uint8_t _atrvBad = 0;  // ATRV unanswered / "?": the adapter cannot measure - module voltage (PID 42) instead
    bool _scanWanted = false, _clearWanted = false;
    uint32_t _codesAt = 0;
    Queued _queue[20] = {};
    uint8_t _nQueue = 0;
    uint8_t _job = kJobNone, _jobArg = 0;
    int8_t _jobIdx = -1;
    obd::Diag _diag;
    obd::DtcList _scanStored, _scanPending, _scanPermanent;  // filled during a scan, published at its end
    obd::FreezeFrame _scanFreeze;
    uint32_t _diagVer = 0;
    char _cmd[12] = "";
    char _line[96] = "";
    size_t _lineLen = 0;
    char _reply[320] = "";  // all lines of the current reply, '\n'-separated (multi-frame VIN / code lists)
    size_t _replyLen = 0;
    char _adapter[24] = "";

    void send(const char* cmd, uint8_t job, uint32_t now);
    void handleReply(Snapshot& out, uint32_t now);
    void handleSearch(uint32_t now);
    void handleJob(Snapshot& out, uint32_t now);
    bool handlePid(Snapshot& out, uint32_t now);
    void goNoEcu(uint32_t now);
    uint32_t replyTimeout() const;
    void push(uint8_t job, uint8_t arg = 0, bool front = false);
    void queueDiscovery();
    void queueCodeScan();
    bool jobApplies(const Queued& q) const;
    const char* nextJob(uint32_t now);
    int pickPid(uint32_t now) const;
    const char* sendPid(int idx, uint32_t now);
    void changed() { _diagVer++; }
};

}  // namespace vehicle
