/**
 * @file Obd.h
 * @brief OBD-II facts shared by the ELM327 driver, the health analysis, the dash and the AI tool (T112) - host
 *        tested, no Arduino: the mode 01 PID table and decoding, trouble codes (mode 03 / 07 / 0A: decode,
 *        describe, rate), VIN (mode 09), multi-frame replies, protocol names, and the diagnosis a source
 *        publishes (Diag)
 * @see wiki/16-Vehicle-Dash.md
 *
 * The meanings of the generic codes are the SAE J2012 / ISO 15031-6 definitions (public standard facts), written
 * short. Codes a carmaker defines itself (P1xxx, B1xxx, C1xxx, U1xxx...) get their system only.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "VehicleData.h"

namespace vehicle {
namespace obd {

// ---------------------------------------------------------------- mode 01 PIDs

/** A mode 01 PID the dash / health / AI can use, and how often it is worth asking for it. */
struct Pid {
    uint8_t pid;
    uint8_t bytes;     // data bytes in the reply
    Channel ch;        // kChannelCount = not a live channel (monitor status, fuel type)
    uint16_t gaugeMs;  // wanted refresh while a dash page shows it (0 = rpm: every other request)
    uint16_t backMs;   // dash closed: only the health analysis and the AI read it (0 = once, at connect)
    uint8_t flags;
};
enum PidFlags : uint8_t {
    kPidBatteryFallback = 1,  // 42 (module voltage): only when the adapter cannot measure (ATRV)
};

/** The table, in priority order (rpm first). */
const Pid* pids(size_t& n);
const Pid* findPid(uint8_t pid);
/** Value of a mode 01 / 02 PID from its data bytes (the formulas of SAE J1979). False if unknown / too short. */
bool decodePid(uint8_t pid, const uint8_t* d, size_t n, float& v);

/** PID 01 01: check-engine light, stored code count, readiness monitors. */
struct Readiness {
    bool valid = false;
    bool mil = false;          // check-engine light on
    uint8_t count = 0;         // codes stored (by the ECUs that answered)
    uint8_t done = 0, total = 0;  // readiness monitors complete / supported
    bool compression = false;  // diesel
};
Readiness decodeMonitor(const uint8_t d[4]);

// ---------------------------------------------------------------- trouble codes

/** Two bytes of a DTC (A B) -> "P0171". `out` holds 6 characters. */
void formatDtc(uint16_t raw, char out[6]);
/** "P0171" / "p0171" -> the two bytes. False if not a code. */
bool parseDtc(const char* text, uint16_t& raw);

struct DtcInfo {
    char text[72];       // what it means (English, short; "" = no meaning known for this exact code)
    const char* system;  // the system it belongs to (English)
    const char* systemTh;
    uint8_t severity;    // 1 = minor (emissions, comfort), 2 = have it checked soon, 3 = serious (engine damage / safety)
    bool generic;        // the same meaning on every car (false = the carmaker's own code)
};
DtcInfo describeDtc(uint16_t raw);

constexpr uint8_t kMaxDtc = 8;
struct DtcList {
    uint16_t code[kMaxDtc] = {};
    uint8_t n = 0;
    void clear() { n = 0; }
    bool has(uint16_t c) const {
        for (uint8_t i = 0; i < n; i++) {
            if (code[i] == c) return true;
        }
        return false;
    }
    /** No duplicates (several ECUs may report one code), 0000 is padding. */
    void add(uint16_t c) {
        if (c && !has(c) && n < kMaxDtc) code[n++] = c;
    }
};

// ---------------------------------------------------------------- replies

/** One message of a reply: a line, or a multi-frame group ("014" / "0:..." / "1:..." on CAN) put together. */
struct Msg {
    uint8_t b[48];
    uint8_t n = 0;
};
/** The messages of an ELM327 reply (lines separated by '\n'). Text lines (SEARCHING..., NO DATA) are skipped. */
size_t messages(const char* reply, Msg* out, size_t cap);
/**
 * Codes from a mode 03 / 07 / 0A reply (`service` = 0x43 / 0x47 / 0x4A). CAN replies start with a count byte;
 * the older buses send three codes per line (0000 = none). protocol 0 = unknown: worked out from the length.
 * Returns how many messages were code replies (0 = NO DATA / not supported).
 */
int parseDtcReply(const Msg* m, size_t n, uint8_t service, uint8_t protocol, DtcList& out);
/** VIN from mode 09 PID 02 (CAN: one multi-frame message; older buses: five numbered lines). */
bool parseVin(const Msg* m, size_t n, char vin[18]);

/** "A6" / "6" (ATDPN, A = found by the automatic search) -> 6. 0 if not a protocol number. */
uint8_t parseProtocol(const char* reply);
const char* protocolName(uint8_t p);
inline bool isCan(uint8_t p) { return p >= 6 && p <= 12; }  // 6-9 ISO 15765-4, A J1939, B / C user CAN

// ---------------------------------------------------------------- what a diagnosing source publishes

struct FreezeFrame {
    uint16_t dtc = 0;  // the code that stored it (0 = none)
    uint8_t n = 0;
    uint8_t pid[8] = {};
    float value[8] = {};
};

enum ClearResult : uint8_t { kClearNone, kClearOk, kClearRefused, kClearFailed };

struct Diag {
    bool available = false;  // the source can read trouble codes (OBD-II, simulator)
    bool simulated = false;
    uint8_t protocol = 0;    // ELM327 protocol number, 0 = unknown
    Readiness ready;
    DtcList stored, pending, permanent;
    bool codesValid = false;  // a code scan has finished
    uint32_t codesAt = 0;     // millis() of the last code scan
    FreezeFrame freeze;
    char vin[18] = "";
    uint8_t fuelType = 0;      // PID 51: 1 petrol, 4 diesel, ... 0 = unknown
    uint8_t clearResult = kClearNone;
    uint8_t supportedPids = 0;  // mode 01 PIDs the car has (of 0x01-0xE0)
    uint32_t scanSeq = 0;       // +1 after each code scan (a clear ends with one)
};

}  // namespace obd
}  // namespace vehicle
