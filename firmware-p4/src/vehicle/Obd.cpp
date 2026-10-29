/**
 * @file Obd.cpp
 * @brief OBD-II PIDs, trouble codes, VIN and reply parsing (see Obd.h)
 */

#include "Obd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace vehicle {
namespace obd {

namespace {

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** Hex digits (spaces allowed) -> bytes appended at out[*n]. False if anything else is in the text. */
bool appendHex(const char* s, uint8_t* out, uint8_t& n, size_t cap) {
    int hi = -1;
    bool any = false;
    for (; *s; s++) {
        if (*s == ' ') continue;
        const int v = hexVal(*s);
        if (v < 0) return false;
        if (hi < 0) {
            hi = v;
        } else {
            if (n < cap) out[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
            any = true;
        }
    }
    return any && hi < 0;
}

}  // namespace

// ---------------------------------------------------------------- PIDs

// Refresh wanted per PID: the fast ones make the gauges move, the slow ones only change over minutes.
// A slow bus (ISO 9141: ~5 requests/s) cannot keep up with all of them: Elm327 serves the most overdue first.
const Pid kPids[] = {
    {0x0C, 2, kRpm, 0, 1000, 0},
    {0x0D, 1, kSpeed, 250, 1000, 0},
    {0x11, 1, kThrottle, 300, 2000, 0},
    {0x0B, 1, kMap, 500, 2000, 0},
    {0x04, 1, kLoad, 800, 2000, 0},
    {0x10, 2, kMaf, 800, 2000, 0},
    {0x05, 1, kCoolant, 2000, 4000, 0},
    {0x0F, 1, kIntake, 3000, 6000, 0},
    {0x06, 1, kStftB1, 1000, 3000, 0},
    {0x07, 1, kLtftB1, 3000, 6000, 0},
    {0x08, 1, kStftB2, 1000, 3000, 0},
    {0x09, 1, kLtftB2, 3000, 6000, 0},
    {0x0E, 1, kTiming, 1000, 5000, 0},
    {0x5E, 2, kFuelRate, 1000, 2000, 0},
    {0x42, 2, kBattery, 2000, 4000, kPidBatteryFallback},
    {0x5C, 1, kOilTemp, 5000, 10000, 0},
    {0x2F, 1, kFuelLevel, 20000, 30000, 0},
    {0x1F, 2, kRunTime, 10000, 30000, 0},
    {0x46, 1, kAmbient, 30000, 60000, 0},
    {0x33, 1, kBaro, 60000, 60000, 0},
    {0x21, 2, kDistMil, 60000, 60000, 0},
    {0x31, 2, kDistClear, 60000, 60000, 0},
    {0x01, 4, kChannelCount, 30000, 30000, 0},  // monitor status: the check-engine light + code count
};

const Pid* pids(size_t& n) {
    n = sizeof(kPids) / sizeof(kPids[0]);
    return kPids;
}

const Pid* findPid(uint8_t pid) {
    for (const Pid& p : kPids) {
        if (p.pid == pid) return &p;
    }
    return nullptr;
}

bool decodePid(uint8_t pid, const uint8_t* d, size_t n, float& v) {
    if (n < 1) return false;
    const float a = d[0], ab = n >= 2 ? d[0] * 256.0f + d[1] : -1;
    switch (pid) {
        case 0x04: case 0x11: case 0x2F: v = a * 100.0f / 255.0f; return true;
        case 0x05: case 0x0F: case 0x46: case 0x5C: v = a - 40.0f; return true;
        case 0x06: case 0x07: case 0x08: case 0x09: v = (a - 128.0f) * 100.0f / 128.0f; return true;
        case 0x0B: case 0x0D: case 0x33: case 0x51: v = a; return true;
        case 0x0E: v = a / 2.0f - 64.0f; return true;
        case 0x0C: if (ab < 0) return false; v = ab / 4.0f; return true;
        case 0x10: if (ab < 0) return false; v = ab / 100.0f; return true;
        case 0x1F: case 0x21: case 0x31: if (ab < 0) return false; v = ab; return true;
        case 0x42: if (ab < 0) return false; v = ab / 1000.0f; return true;
        case 0x5E: if (ab < 0) return false; v = ab / 20.0f; return true;
        default: return false;
    }
}

Readiness decodeMonitor(const uint8_t d[4]) {
    Readiness r;
    auto bits = [](uint8_t x) {
        int c = 0;
        for (; x; x &= (uint8_t)(x - 1)) c++;
        return c;
    };
    r.valid = true;
    r.mil = (d[0] & 0x80) != 0;
    r.count = d[0] & 0x7F;
    r.compression = (d[1] & 0x08) != 0;
    // B: continuous monitors (misfire, fuel system, components) supported in bits 0-2, incomplete in 4-6;
    // C / D: the other monitors supported / incomplete
    const uint8_t contSup = d[1] & 0x07, contInc = (d[1] >> 4) & contSup;
    r.total = (uint8_t)(bits(contSup) + bits(d[2]));
    const int incomplete = bits(contInc) + bits(d[3] & d[2]);
    r.done = (uint8_t)(r.total > incomplete ? r.total - incomplete : 0);
    return r;
}

// ---------------------------------------------------------------- trouble codes

void formatDtc(uint16_t raw, char out[6]) {
    static const char kLetter[] = "PCBU";
    static const char kHex[] = "0123456789ABCDEF";
    out[0] = kLetter[raw >> 14];
    out[1] = kHex[(raw >> 12) & 3];
    out[2] = kHex[(raw >> 8) & 15];
    out[3] = kHex[(raw >> 4) & 15];
    out[4] = kHex[raw & 15];
    out[5] = '\0';
}

bool parseDtc(const char* t, uint16_t& raw) {
    if (!t) return false;
    while (*t == ' ') t++;
    const char* letters = "PCBU";
    const char* l = strchr(letters, toupper((unsigned char)t[0]));
    if (!t[0] || !l) return false;
    if (t[1] < '0' || t[1] > '3') return false;
    int v = 0;
    for (int i = 2; i < 5; i++) {
        const int h = hexVal(t[i]);
        if (h < 0) return false;
        v = v << 4 | h;
    }
    if (isalnum((unsigned char)t[5])) return false;
    raw = (uint16_t)((l - letters) << 14 | (t[1] - '0') << 12 | v);
    return true;
}

namespace {

struct Known {
    uint16_t code;
    uint8_t severity;
    const char* text;
};

// SAE J2012 meanings of the codes seen most often (sorted by code). Ranges with a cylinder / sensor number in
// them (misfire P0301-P0312, injector, ignition coil, O2 sensors) are worked out in describeDtc().
const Known kKnown[] = {
    {0x0010, 2, "Camshaft position actuator circuit (bank 1)"},
    {0x0011, 2, "Camshaft timing over-advanced (bank 1)"},
    {0x0012, 2, "Camshaft timing over-retarded (bank 1)"},
    {0x0016, 2, "Crankshaft / camshaft position correlation (bank 1)"},
    {0x0087, 3, "Fuel rail / system pressure too low"},
    {0x0088, 2, "Fuel rail / system pressure too high"},
    {0x0100, 2, "Mass air flow (MAF) sensor circuit"},
    {0x0101, 2, "Mass air flow (MAF) sensor range / performance"},
    {0x0102, 2, "Mass air flow (MAF) sensor circuit low"},
    {0x0103, 2, "Mass air flow (MAF) sensor circuit high"},
    {0x0105, 2, "Manifold pressure (MAP) sensor circuit"},
    {0x0106, 2, "Manifold pressure (MAP) sensor range / performance"},
    {0x0107, 2, "Manifold pressure (MAP) sensor circuit low"},
    {0x0108, 2, "Manifold pressure (MAP) sensor circuit high"},
    {0x0110, 1, "Intake air temperature sensor circuit"},
    {0x0112, 1, "Intake air temperature sensor circuit low"},
    {0x0113, 1, "Intake air temperature sensor circuit high"},
    {0x0115, 2, "Engine coolant temperature sensor circuit"},
    {0x0116, 2, "Engine coolant temperature sensor range / performance"},
    {0x0117, 2, "Engine coolant temperature sensor circuit low"},
    {0x0118, 2, "Engine coolant temperature sensor circuit high"},
    {0x0120, 2, "Throttle position sensor A circuit"},
    {0x0121, 2, "Throttle position sensor A range / performance"},
    {0x0122, 2, "Throttle position sensor A circuit low"},
    {0x0123, 2, "Throttle position sensor A circuit high"},
    {0x0125, 1, "Coolant temperature too low for closed loop fuel control"},
    {0x0128, 1, "Coolant below thermostat regulating temperature (thermostat)"},
    {0x0171, 2, "System too lean (bank 1)"},
    {0x0172, 2, "System too rich (bank 1)"},
    {0x0174, 2, "System too lean (bank 2)"},
    {0x0175, 2, "System too rich (bank 2)"},
    {0x0190, 2, "Fuel rail pressure sensor circuit"},
    {0x0200, 2, "Injector circuit"},
    {0x0217, 3, "Engine overheat condition"},
    {0x0219, 2, "Engine overspeed condition"},
    {0x0221, 2, "Throttle position sensor B range / performance"},
    {0x0234, 2, "Turbo / supercharger overboost"},
    {0x0299, 2, "Turbo / supercharger underboost"},
    {0x0300, 3, "Random / multiple cylinder misfire detected"},
    {0x0325, 2, "Knock sensor 1 circuit (bank 1)"},
    {0x0326, 1, "Knock sensor 1 range / performance (bank 1)"},
    {0x0327, 1, "Knock sensor 1 circuit low (bank 1)"},
    {0x0328, 1, "Knock sensor 1 circuit high (bank 1)"},
    {0x0335, 3, "Crankshaft position sensor A circuit"},
    {0x0336, 3, "Crankshaft position sensor A range / performance"},
    {0x0340, 2, "Camshaft position sensor A circuit (bank 1)"},
    {0x0341, 2, "Camshaft position sensor A range / performance (bank 1)"},
    {0x0380, 2, "Glow plug / heater circuit A"},
    {0x0400, 1, "Exhaust gas recirculation (EGR) flow"},
    {0x0401, 1, "EGR flow insufficient"},
    {0x0402, 2, "EGR flow excessive"},
    {0x0403, 1, "EGR control circuit"},
    {0x0404, 1, "EGR control circuit range / performance"},
    {0x0410, 1, "Secondary air injection system"},
    {0x0420, 2, "Catalyst efficiency below threshold (bank 1)"},
    {0x0421, 2, "Warm-up catalyst efficiency below threshold (bank 1)"},
    {0x0430, 2, "Catalyst efficiency below threshold (bank 2)"},
    {0x0440, 1, "Evaporative emission (EVAP) system"},
    {0x0441, 1, "EVAP system incorrect purge flow"},
    {0x0442, 1, "EVAP system small leak detected"},
    {0x0443, 1, "EVAP purge control valve circuit"},
    {0x0446, 1, "EVAP vent control circuit"},
    {0x0455, 1, "EVAP system large leak detected (check the fuel cap)"},
    {0x0456, 1, "EVAP system very small leak detected"},
    {0x0457, 1, "EVAP leak detected: fuel cap loose / off"},
    {0x0461, 1, "Fuel level sensor circuit range / performance"},
    {0x0480, 2, "Cooling fan 1 control circuit"},
    {0x0500, 2, "Vehicle speed sensor A"},
    {0x0501, 2, "Vehicle speed sensor A range / performance"},
    {0x0505, 2, "Idle air control system"},
    {0x0506, 1, "Idle control system RPM lower than expected"},
    {0x0507, 1, "Idle control system RPM higher than expected"},
    {0x0520, 2, "Engine oil pressure sensor / switch circuit"},
    {0x0521, 2, "Engine oil pressure sensor range / performance"},
    {0x0522, 2, "Engine oil pressure sensor / switch low voltage"},
    {0x0524, 3, "Engine oil pressure too low"},
    {0x0530, 1, "A/C refrigerant pressure sensor circuit"},
    {0x0562, 2, "System voltage low"},
    {0x0563, 2, "System voltage high"},
    {0x0571, 1, "Brake switch A circuit"},
    {0x0600, 2, "Serial communication link"},
    {0x0601, 3, "Control module memory check sum error"},
    {0x0603, 2, "Control module keep-alive memory error"},
    {0x0604, 3, "Control module RAM error"},
    {0x0605, 3, "Control module ROM error"},
    {0x0606, 3, "Control module processor fault"},
    {0x0627, 3, "Fuel pump control circuit open"},
    {0x0641, 2, "Sensor reference voltage A circuit open"},
    {0x0700, 2, "Transmission control system (the gearbox module has its own code)"},
    {0x0705, 2, "Transmission range sensor circuit"},
    {0x0715, 2, "Input / turbine speed sensor circuit"},
    {0x0720, 2, "Output speed sensor circuit"},
    {0x0730, 2, "Incorrect gear ratio"},
    {0x0740, 2, "Torque converter clutch circuit"},
    {0x0741, 2, "Torque converter clutch stuck off"},
    {0x0750, 2, "Shift solenoid A"},
    {0x0755, 2, "Shift solenoid B"},
    {0x0760, 2, "Shift solenoid C"},
    {0x0850, 1, "Park / neutral switch input circuit"},
    {0x2096, 1, "Post-catalyst fuel trim too lean (bank 1)"},
    {0x2097, 1, "Post-catalyst fuel trim too rich (bank 1)"},
    {0x2135, 3, "Throttle position sensor A / B voltage correlation"},
    {0x2138, 3, "Accelerator pedal position sensor D / E voltage correlation"},
    {0x2195, 1, "O2 sensor signal stuck lean (bank 1 sensor 1)"},
    {0x2196, 1, "O2 sensor signal stuck rich (bank 1 sensor 1)"},
    {0x2270, 1, "O2 sensor signal stuck lean (bank 1 sensor 2)"},
    {0x2271, 1, "O2 sensor signal stuck rich (bank 1 sensor 2)"},
    {0xC073, 2, "Control module communication bus off"},
    {0xC100, 3, "Lost communication with the engine control module (ECM / PCM)"},
    {0xC101, 2, "Lost communication with the transmission control module"},
    {0xC121, 2, "Lost communication with the ABS module"},
    {0xC140, 2, "Lost communication with the body control module"},
    {0xC155, 1, "Lost communication with the instrument cluster"},
};

/** The last three characters as a decimal number (most generic codes use only 0-9), -1 if not decimal. */
int decimalPart(uint16_t raw) {
    int v = 0;
    for (int shift = 8; shift >= 0; shift -= 4) {
        const int d = (raw >> shift) & 15;
        if (d > 9) return -1;
        v = v * 10 + d;
    }
    return v;
}

}  // namespace

DtcInfo describeDtc(uint16_t raw) {
    DtcInfo info;
    info.text[0] = '\0';
    info.severity = 2;
    const uint8_t letter = raw >> 14, digit = (raw >> 12) & 3, group = (raw >> 8) & 15;
    if (letter == 0) {  // P: 0 and 2 SAE, 3 SAE from P34, 1 the carmaker's
        info.generic = digit == 0 || digit == 2 || (digit == 3 && group >= 4);
        static const char* const kSys[] = {"fuel and air metering", "fuel and air metering", "fuel and air metering (injectors)",
                                           "ignition system / misfire", "emission controls", "speed, idle control and inputs",
                                           "engine computer and output circuits", "transmission", "transmission",
                                           "transmission", "hybrid propulsion", "hybrid propulsion"};
        static const char* const kSysTh[] = {"ระบบน้ำมัน/อากาศ", "ระบบน้ำมัน/อากาศ", "ระบบหัวฉีด", "ระบบจุดระเบิด/เครื่องสะดุด",
                                             "ระบบควบคุมไอเสีย", "ความเร็ว/รอบเดินเบา", "กล่อง ECU/วงจรควบคุม", "ระบบเกียร์",
                                             "ระบบเกียร์", "ระบบเกียร์", "ระบบไฮบริด", "ระบบไฮบริด"};
        info.system = group < 12 ? kSys[group] : "powertrain";
        info.systemTh = group < 12 ? kSysTh[group] : "ระบบส่งกำลัง";
        if (group == 3) info.severity = 3;  // ignition / misfire: unburnt fuel overheats the catalyst
    } else {
        info.generic = digit == 0 || digit == 3;
        info.system = letter == 1 ? "chassis (brakes / ABS, steering, suspension)"
                      : letter == 2 ? "body (airbags, A/C, lights, locks)"
                                    : "network (modules not talking to each other)";
        info.systemTh = letter == 1 ? "ช่วงล่าง/เบรก ABS" : letter == 2 ? "ตัวถัง/ถุงลม/แอร์" : "การสื่อสารระหว่างกล่อง";
    }
    if (!info.generic) return info;

    // exact codes
    size_t lo = 0, hi = sizeof(kKnown) / sizeof(kKnown[0]);
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (kKnown[mid].code < raw) lo = mid + 1;
        else hi = mid;
    }
    if (lo < sizeof(kKnown) / sizeof(kKnown[0]) && kKnown[lo].code == raw) {
        snprintf(info.text, sizeof(info.text), "%s", kKnown[lo].text);
        info.severity = kKnown[lo].severity;
        return info;
    }
    // numbered ranges of the P0 codes
    const int num = letter == 0 && digit == 0 ? decimalPart(raw) : -1;
    if (num >= 301 && num <= 312) {
        snprintf(info.text, sizeof(info.text), "Cylinder %d misfire detected", num - 300);
        info.severity = 3;
    } else if (num >= 201 && num <= 212) {
        snprintf(info.text, sizeof(info.text), "Injector circuit, cylinder %d", num - 200);
        info.severity = 2;
    } else if (num >= 351 && num <= 362) {
        snprintf(info.text, sizeof(info.text), "Ignition coil %c primary / secondary circuit (cylinder %d)",
                 'A' + (num - 351), num - 350);
        info.severity = 3;
    } else if (num >= 130 && num <= 167 && num != 148 && num != 149) {
        static const char* const kWhat[] = {"circuit", "circuit low voltage", "circuit high voltage", "slow response",
                                            "no activity detected", "heater circuit"};
        const int base = num >= 150 ? num - 150 : num - 130;  // 6 codes per sensor
        const int bank = num >= 150 ? 2 : 1, sensor = base / 6 + 1;
        snprintf(info.text, sizeof(info.text), "O2 sensor %s (bank %d sensor %d)", kWhat[base % 6], bank, sensor);
        info.severity = 1;
    }
    return info;
}

// ---------------------------------------------------------------- replies

size_t messages(const char* reply, Msg* out, size_t cap) {
    size_t n = 0;
    int multi = -1;          // the multi-frame message being put together
    unsigned expect = 0;     // its length from the "014" line before it (0 = unknown)
    unsigned lenLine = 0;
    char line[100];
    for (const char* p = reply; p && *p;) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = nl ? nl + 1 : nullptr;
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\r')) line[--len] = '\0';
        const char* s = line;
        while (*s == ' ') s++;
        if (!*s) continue;
        const char* colon = strchr(s, ':');
        if (colon && colon - s >= 1 && colon - s <= 2 && hexVal(s[0]) >= 0 && (colon - s == 1 || hexVal(s[1]) >= 0)) {
            const int idx = hexVal(s[colon - s - 1]);  // frame number (its low digit: they count 0-F, 0-F ...)
            if (idx == 0 || multi < 0) {
                if (n >= cap) break;
                multi = (int)n++;
                out[multi].n = 0;
                expect = lenLine;
                lenLine = 0;
            }
            Msg& m = out[multi];
            appendHex(colon + 1, m.b, m.n, sizeof(m.b));
            if (expect && m.n >= expect) {
                m.n = (uint8_t)(expect < sizeof(m.b) ? expect : sizeof(m.b));  // the padding of the last frame
                multi = -1;
            }
            continue;
        }
        size_t digits = 0;
        bool hexOnly = true;
        for (const char* q = s; *q; q++) {
            if (*q == ' ') continue;
            if (hexVal(*q) < 0) hexOnly = false;
            digits++;
        }
        if (hexOnly && digits == 3) {  // "014": a multi-frame of 0x14 bytes follows
            lenLine = (unsigned)strtoul(s, nullptr, 16);
            continue;
        }
        if (!hexOnly || n >= cap) continue;  // SEARCHING..., NO DATA, BUS INIT...
        Msg& m = out[n];
        m.n = 0;
        if (appendHex(s, m.b, m.n, sizeof(m.b))) n++;
        multi = -1;
    }
    return n;
}

int parseDtcReply(const Msg* m, size_t n, uint8_t service, uint8_t protocol, DtcList& out) {
    int replies = 0;
    for (size_t i = 0; i < n; i++) {
        const Msg& r = m[i];
        if (r.n < 1 || r.b[0] != service) continue;
        replies++;
        // CAN: 43 <count> <A B>...; older buses: 43 <A B> <A B> <A B> (7 bytes, 0000 = no code)
        const bool can = protocol ? isCan(protocol) : (r.n >= 2 && (size_t)(r.n - 2) == (size_t)r.b[1] * 2 && r.n != 7);
        if (can) {
            const uint8_t count = r.n >= 2 ? r.b[1] : 0;
            for (uint8_t k = 0; k < count && 3 + 2 * k < r.n; k++) out.add((uint16_t)(r.b[2 + 2 * k] << 8 | r.b[3 + 2 * k]));
        } else {
            for (uint8_t k = 1; k + 1 < r.n; k += 2) out.add((uint16_t)(r.b[k] << 8 | r.b[k + 1]));
        }
    }
    return replies;
}

bool parseVin(const Msg* m, size_t n, char vin[18]) {
    uint8_t data[40];
    size_t len = 0;
    bool numbered[6] = {};
    uint8_t parts[6][4] = {};
    for (size_t i = 0; i < n; i++) {
        const Msg& r = m[i];
        if (r.n < 4 || r.b[0] != 0x49 || r.b[1] != 0x02) continue;
        if (r.n >= 20) {  // CAN: 49 02 01 + 17 characters in one message
            for (uint8_t k = 3; k < r.n && len < sizeof(data); k++) data[len++] = r.b[k];
            break;
        }
        const uint8_t seq = r.b[2];  // ISO 9141 / KWP / J1850: 49 02 <1-5> + 4 bytes
        if (seq >= 1 && seq <= 5 && r.n >= 7) {
            numbered[seq] = true;
            memcpy(parts[seq], r.b + 3, 4);
        }
    }
    if (!len) {
        for (int s = 1; s <= 5; s++) {
            if (!numbered[s]) return false;
            memcpy(data + len, parts[s], 4);
            len += 4;
        }
    }
    // the VIN is the last 17 characters (the first message pads with 00; a frame without its length line ends in padding)
    while (len && !isalnum(data[len - 1])) len--;
    size_t start = len;
    int count = 0;
    while (start > 0 && count < 17 && isalnum(data[start - 1])) {
        start--;
        count++;
    }
    if (count < 17) return false;
    for (int k = 0; k < 17; k++) vin[k] = (char)toupper(data[start + k]);
    vin[17] = '\0';
    return true;
}

uint8_t parseProtocol(const char* reply) {
    if (!reply) return 0;
    const char* s = reply;
    while (*s == ' ' || *s == '\n' || *s == '\r') s++;
    if (*s == 'A' || *s == 'a') {
        if (hexVal(s[1]) < 0) return 0;
        s++;
    }
    const int v = hexVal(*s);
    if (v < 1 || v > 12) return 0;
    s++;
    while (*s == ' ' || *s == '\n' || *s == '\r') s++;
    return *s ? 0 : (uint8_t)v;
}

const char* protocolName(uint8_t p) {
    static const char* const kNames[] = {"unknown",
                                         "SAE J1850 PWM",
                                         "SAE J1850 VPW",
                                         "ISO 9141-2",
                                         "ISO 14230-4 KWP2000 (5 baud init)",
                                         "ISO 14230-4 KWP2000 (fast init)",
                                         "ISO 15765-4 CAN 11 bit 500 kbaud",
                                         "ISO 15765-4 CAN 29 bit 500 kbaud",
                                         "ISO 15765-4 CAN 11 bit 250 kbaud",
                                         "ISO 15765-4 CAN 29 bit 250 kbaud",
                                         "SAE J1939 CAN",
                                         "user CAN 1",
                                         "user CAN 2"};
    return p < sizeof(kNames) / sizeof(kNames[0]) ? kNames[p] : kNames[0];
}

}  // namespace obd
}  // namespace vehicle
