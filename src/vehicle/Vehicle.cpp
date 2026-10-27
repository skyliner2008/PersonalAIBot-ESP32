/**
 * @file Vehicle.cpp
 * @brief Vehicle dash back end (see Vehicle.h)
 */

#include "Vehicle.h"

#include <Preferences.h>
#include <esp_heap_caps.h>

#include <new>

#include "../app/AppConfig.h"  // FIRMWARE_VERSION (the log's first event)
#include "../phone/PhoneLink.h"
#include "../system/Peripherals.h"
#include "../system/PsramTask.h"
#include "../util/JsonAlloc.h"
#include "../util/JsonFile.h"
#include "../util/PsramAlloc.h"
#include "Elm327.h"
#include "SimEngine.h"
#include "VehicleLink.h"
#include "VehicleLog.h"

Vehicle gVehicle;

using namespace vehicle;

struct Vehicle::Shared {
    Snapshot snap;
    obd::Diag diag;
    Health health;
    bool speedFromGps = false;
};

namespace {

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m); }
};

constexpr uint32_t kHealthEveryMs = 250;
constexpr uint32_t kBleOffAfterMs = 15000;  // dash out of sight this long = Bluetooth off (its settings page is a visit)
constexpr uint32_t kSimScanMs = 1200;  // the simulator "reads" its codes about as long as a real CAN car takes

bool simulated(SourceId s) { return s == SourceId::SimAuto || s == SourceId::SimTouch; }

float roundTo(float v, int decimals) {
    const float k = decimals == 2 ? 100.0f : decimals == 1 ? 10.0f : 1.0f;
    return roundf(v * k) / k;
}

/** {"code":"P0171","meaning":"...","severity":2[,"system":"...","generic":true]} */
void addCode(JsonArray a, uint16_t raw, const char* status, bool full) {
    char code[6];
    obd::formatDtc(raw, code);
    const obd::DtcInfo info = obd::describeDtc(raw);
    JsonObject o = a.add<JsonObject>();
    o["code"] = code;
    if (status) o["status"] = status;
    o["meaning"] = info.text[0] ? (const char*)info.text : info.generic ? "generic code (meaning not in the table)"
                                                                       : "the carmaker's own code: see its service manual";
    o["severity"] = info.severity;
    if (full || !info.text[0]) o["system"] = info.system;
}

const char* fuelFromKey(uint8_t f) {
    static const char* const kKeys[] = {"none", "fuel_rate_pid", "maf", "map_estimate"};
    return f < 4 ? kKeys[f] : "none";
}

}  // namespace

bool Vehicle::bleKeptAtBoot() { return vehicleBleKeptAtBoot(); }

// ---------------------------------------------------------------- settings

void Vehicle::begin(const char* agentDir) {
    _lock = xSemaphoreCreateMutex();
    // the shared state (~600 bytes) and the health analysis live in PSRAM: internal RAM is the assistant's TLS
    if (void* m = psramMalloc(sizeof(Shared))) _sh = new (m) Shared();
    if (void* m = psramMalloc(sizeof(HealthMonitor))) _hm = new (m) HealthMonitor();
    snprintf(_dir, sizeof(_dir), "%s", agentDir ? agentDir : "");
    Preferences p;
    if (p.begin("veh", true)) {
        const uint8_t src = p.getUChar("src", 0);
        _s.source = src < (uint8_t)SourceId::Count ? (SourceId)src : SourceId::Off;
        _s.kind = p.getUChar("kind", 0) ? Kind::Motorcycle : Kind::Car;
        _s.shiftRpm = p.getUShort("shift", 0);
        _s.rpmMax = p.getUShort("rpmmax", 0);
        p.getString("ble", _s.bleAddr, sizeof(_s.bleAddr));
        _s.bleType = p.getUChar("bletype", 0);
        _s.elmBaud = p.getULong("elmbaud", _s.elmBaud);
        _s.textBaud = p.getULong("txtbaud", _s.textBaud);
        const uint8_t style = p.getUChar("style", 0);
        _s.style = style < (uint8_t)DashStyle::Count ? (DashStyle)style : DashStyle::Race;
        _s.protocol = p.getUChar("proto", 0);
        _s.log = p.getBool("log", true);
        p.getString("rfile", _s.replayFile, sizeof(_s.replayFile));
        p.end();
    }
    // agent/vehicle.json: {"kline": {"table": 17, "rpm": 0, "coolant": 5, ...}} - which byte holds what on your bike;
    // "engine_l" / "fuel": the engine size and fuel for the fuel estimate of a car without a MAF / fuel-rate PID
    if (_dir[0]) {
        JsonDocument doc(JsonPsramAllocator::instance());
        const String path = String(_dir) + "/vehicle.json";
        if (jsonfile::load(path.c_str(), doc)) {
            hondak::parseLayout(doc["kline"].as<JsonObjectConst>(), _layout);
            const float litres = doc["engine_l"] | 0.0f;
            if (litres >= 0.05f && litres <= 10.0f) _engineL = litres;
            _diesel = !strcmp(doc["fuel"] | "", "diesel");
            Serial.printf("[Vehicle] %s: K-Line table 0x%02X, engine %.2f l%s\n", path.c_str(), _layout.table, _engineL,
                          _diesel ? " diesel" : "");
        }
    }
    if (usesUart(_s.source) && (gPeriph.usesPin(UartLink::kTx) || gPeriph.usesPin(UartLink::kRx))) {
        Serial.println("[Vehicle] GPIO 43/44 belong to a device in devices.json - source turned off");
        _s.source = SourceId::Off;
    }
    gVehicleLog.begin(_dir);  // T115: the OBD log on the SD card (off without one)
    gVehicleLog.setEnabled(_s.log);
    if (_s.source == SourceId::ObdBle && !kBleBuilt) {
        Serial.println("[Vehicle] OBD over Bluetooth is not in this firmware (VEHICLE_BLE=0) - source turned off");
        _s.source = SourceId::Off;
    }
    snprintf(_status, sizeof(_status), "%s", _s.source == SourceId::Off ? "OFF" : "START");
    Serial.printf("[Vehicle] Source %s (%s)%s\n", sourceKey(_s.source), _s.kind == Kind::Car ? "car" : "motorcycle",
                  bleKeptAtBoot() ? " - BLE memory kept" : "");
    if (_s.source != SourceId::Off) startTask();
}

void Vehicle::save() {  // the caller's task: NVS needs an internal stack (console, UI, agent)
    Settings s = settings();
    Preferences p;
    if (!p.begin("veh", false)) return;
    p.putUChar("src", (uint8_t)s.source);
    p.putUChar("kind", s.kind == Kind::Motorcycle ? 1 : 0);
    p.putUShort("shift", s.shiftRpm);
    p.putUShort("rpmmax", s.rpmMax);
    p.putString("ble", s.bleAddr);
    p.putUChar("bletype", s.bleType);
    p.putULong("elmbaud", s.elmBaud);
    p.putULong("txtbaud", s.textBaud);
    p.putUChar("style", (uint8_t)s.style);
    p.putUChar("proto", s.protocol);
    p.putBool("log", s.log);
    p.putString("rfile", s.replayFile);
    p.end();
}

// what the vehicle task learned (it has a PSRAM stack: no NVS there) - saved from the agent's task
void Vehicle::service() {
    const uint8_t proto = _learnedProto;
    const uint32_t baud = _learnedBaud;
    bool dirty = _learnedBle;  // a Bluetooth adapter found by a scan (its address is already in _s)
    if (!proto && !baud && !dirty) return;
    _learnedProto = 0;
    _learnedBaud = 0;
    _learnedBle = false;
    {
        Guard g(_lock);
        if (proto && proto != _s.protocol) _s.protocol = proto, dirty = true;
        if (baud && baud != _s.elmBaud) _s.elmBaud = baud, dirty = true;  // no restart: the link already runs at it
    }
    if (!dirty) return;
    save();
    const Settings s = settings();
    Serial.printf("[Vehicle] Learned: protocol %u (%s), ELM327 %lu baud, Bluetooth adapter %s\n", s.protocol,
                  obd::protocolName(s.protocol), (unsigned long)s.elmBaud, s.bleAddr[0] ? s.bleAddr : "-");
}

HealthConfig Vehicle::healthConfig() {
    const Settings s = settings();
    const DashConfig d = dashConfig();
    HealthConfig c;
    c.kind = s.kind;
    c.engineL = _engineL > 0 ? _engineL : (s.kind == Kind::Car ? 1.5f : 0.125f);
    c.diesel = _diesel;
    c.coolantWarn = d.coolantWarn;
    c.coolantCrit = d.coolantCrit;
    return c;
}

uint8_t Vehicle::focus() {
    const uint32_t at = _visibleAt;
    if (!at || (int32_t)(millis() - at) > 2000) return (uint8_t)Elm327::Focus::Background;  // signed: see runElm
    return (uint8_t)(_visibleInfo ? Elm327::Focus::Info : Elm327::Focus::Gauge);
}

Vehicle::Settings Vehicle::settings() {
    if (!_lock) return _s;
    Guard g(_lock);
    return _s;
}

DashConfig Vehicle::dashConfig() {
    const Settings s = settings();
    DashConfig c = defaultConfig(s.kind);
    if (s.rpmMax >= 3000 && s.rpmMax <= 20000) c.rpmMax = s.rpmMax;
    if (s.shiftRpm >= 2000 && s.shiftRpm <= c.rpmMax) c.shiftRpm = s.shiftRpm;
    if (c.shiftRpm > c.rpmMax) c.shiftRpm = c.rpmMax;
    return c;
}

const char* Vehicle::setSource(SourceId src) {
    if ((uint8_t)src >= (uint8_t)SourceId::Count) return "ไม่รู้จักแหล่งข้อมูลนี้";
    if (src == SourceId::ObdBle && !kBleBuilt) {
        return "เฟิร์มแวร์นี้ไม่มี OBD ผ่าน Bluetooth (กินหน่วยความจำที่ผู้ช่วยต้องใช้) — ใช้ ELM327 แบบสายแทน";
    }
    if (usesUart(src) && (gPeriph.usesPin(UartLink::kTx) || gPeriph.usesPin(UartLink::kRx))) {
        return "GPIO 43/44 ถูกใช้โดยอุปกรณ์ใน devices.json อยู่ — ย้ายอุปกรณ์นั้นก่อน";
    }
    {
        Guard g(_lock);
        _s.source = src;
        _restart = true;
    }
    _announced.clear();  // the simulator's codes are not this car's
    save();
    if (src != SourceId::Off && !_task) startTask();
    if (_task) xTaskNotifyGive(_task);
    Serial.printf("[Vehicle] Source -> %s%s\n", sourceKey(src), needsRestart() ? " (restart once to start Bluetooth)" : "");
    return nullptr;
}

bool Vehicle::openDash() {  // the caller's task: setSource writes NVS (UI / agent: internal stacks)
    const bool startSim = source() == SourceId::Off;
    if (startSim) setSource(SourceId::SimAuto);
    requestOpen();
    return startSim;
}

void Vehicle::setKind(Kind k) {
    {
        Guard g(_lock);
        _s.kind = k;
        _s.shiftRpm = _s.rpmMax = 0;  // the new kind's defaults
        _restart = true;
    }
    save();
    if (_task) xTaskNotifyGive(_task);
}

void Vehicle::setShiftRpm(int rpm) {
    {
        Guard g(_lock);
        _s.shiftRpm = (uint16_t)(rpm < 0 ? 0 : (rpm > 20000 ? 20000 : rpm));
    }
    save();
}

void Vehicle::setRpmMax(int rpm) {
    {
        Guard g(_lock);
        _s.rpmMax = (uint16_t)(rpm < 0 ? 0 : (rpm > 20000 ? 20000 : rpm));
    }
    save();
}

void Vehicle::forgetBleAdapter() {
    {
        Guard g(_lock);
        _s.bleAddr[0] = '\0';
        _s.bleType = 0;
        _restart = true;
    }
    save();
    if (_task) xTaskNotifyGive(_task);
}

void Vehicle::setStyle(DashStyle s) {
    if ((uint8_t)s >= (uint8_t)DashStyle::Count) return;
    {
        Guard g(_lock);
        _s.style = s;
    }
    save();
}

void Vehicle::setBaud(bool elm, uint32_t baud) {
    if (baud < 1200 || baud > 2000000) return;
    {
        Guard g(_lock);
        (elm ? _s.elmBaud : _s.textBaud) = baud;
        _restart = true;
    }
    save();
    if (_task) xTaskNotifyGive(_task);
}

void Vehicle::setLogEnabled(bool on) {
    {
        Guard g(_lock);
        _s.log = on;
    }
    save();
    gVehicleLog.setEnabled(on);  // off: the open file is closed now; on: from the next run of a source
}

const char* Vehicle::startReplay(const char* name, float speed) {
    if (!gVehicleLog.available()) return "ไม่มี SD card: log OBD เก็บได้เฉพาะบน SD card";
    char path[128];
    if (!gVehicleLog.path(name, path, sizeof(path))) return "ไม่พบไฟล์ log นี้ใน SD card (agent/obdlog)";
    const char* file = strrchr(path, '/') + 1;
    {
        Guard g(_lock);
        snprintf(_s.replayFile, sizeof(_s.replayFile), "%s", name && *name && strcmp(name, "latest") ? file : "");
    }
    _replaySpeed = speed >= 0.1f && speed <= 50.0f ? speed : 1.0f;
    if (source() == SourceId::Replay) {  // another file / speed: the runner starts over
        save();
        _restart = true;
        if (_task) xTaskNotifyGive(_task);
        return nullptr;
    }
    return setSource(SourceId::Replay);
}

bool Vehicle::needsRestart() { return source() == SourceId::ObdBle && !bleKeptAtBoot(); }

// ---------------------------------------------------------------- shared readings

Snapshot Vehicle::snapshot() {
    Snapshot s;
    if (!_sh) return s;
    portENTER_CRITICAL(&_mux);
    s = _sh->snap;
    portEXIT_CRITICAL(&_mux);
    return s;
}

obd::Diag Vehicle::diag() {
    obd::Diag d;
    if (!_sh) return d;
    portENTER_CRITICAL(&_mux);
    d = _sh->diag;
    portEXIT_CRITICAL(&_mux);
    return d;
}

Health Vehicle::health() {
    Health h;
    if (!_sh) return h;
    portENTER_CRITICAL(&_mux);
    h = _sh->health;
    portEXIT_CRITICAL(&_mux);
    return h;
}

bool Vehicle::speedFromGps() {
    if (!_sh) return false;
    portENTER_CRITICAL(&_mux);
    const bool g = _sh->speedFromGps;
    portEXIT_CRITICAL(&_mux);
    return g;
}

uint32_t Vehicle::requestScan() {
    const obd::Diag d = diag();
    if (!d.available) return 0;
    // a real car must be connected (in NO ECU the request would only wait): say so instead
    if (!d.simulated && status() != "LIVE" && status() != "ECU SCAN") return 0;
    _scanReq = true;
    return d.scanSeq + 1;
}

const char* Vehicle::requestClear(uint32_t& seq) {
    seq = 0;
    const obd::Diag d = diag();
    if (!d.available) return "แหล่งข้อมูลนี้อ่าน/ล้างรหัสปัญหาไม่ได้ (ต้องใช้ OBD-II ผ่าน ELM327)";
    if (!d.simulated) {
        if (status() != "LIVE") return "ยังไม่ได้เชื่อมต่อกับกล่อง ECU ของรถ (เปิดสวิตช์กุญแจหรือยัง?)";
        const Snapshot s = snapshot();
        if (s.fresh(kRpm, millis()) && s.value(kRpm) > 400) {
            return "ต้องดับเครื่องยนต์ก่อน (บิดกุญแจค้างไว้ที่ ON) แล้วค่อยล้างรหัส";
        }
    }
    seq = d.scanSeq + 1;
    _clearReq = true;
    return nullptr;
}

bool Vehicle::scanDone(uint32_t seq) { return seq && (int32_t)(diag().scanSeq - seq) >= 0; }

// the runners' diagnosis: scanSeq counts every finished scan across sources (each runner starts from 0)
void Vehicle::publishDiag(const obd::Diag& in) {
    if (!_sh) return;
    obd::Diag d = in;
    if (in.scanSeq != _lastInSeq) {
        _lastInSeq = in.scanSeq;
        if (in.codesValid) _scanCount++;
    }
    d.scanSeq = _scanCount;
    portENTER_CRITICAL(&_mux);
    _sh->diag = d;
    portEXIT_CRITICAL(&_mux);
    if (!gVehicleLog.active() || !d.available) return;
    // T115: the diagnosis as it changes - the light, codes, readiness, VIN, protocol
    auto codes = [](const obd::DtcList& l, char* out, size_t cap) {
        size_t o = 0;
        out[0] = '\0';
        for (uint8_t i = 0; i < l.n && o + 7 < cap; i++) {
            if (o) out[o++] = ',';
            obd::formatDtc(l.code[i], out + o);
            o += 5;
        }
        out[o] = '\0';
        return out;
    };
    char st[64], pe[64], pm[64], fz[8] = "", text[320];
    if (d.freeze.dtc) obd::formatDtc(d.freeze.dtc, fz);
    snprintf(text, sizeof(text),
             "mil=%d count=%u ready=%u/%u proto=%u pids=%u fuel=%u vin=%s stored=%s pending=%s permanent=%s freeze=%s "
             "clear=%u codes_read=%d scans=%lu",
             d.ready.mil, d.ready.count, d.ready.done, d.ready.total, d.protocol, d.supportedPids, d.fuelType,
             d.vin[0] ? d.vin : "-", codes(d.stored, st, sizeof(st)), codes(d.pending, pe, sizeof(pe)),
             codes(d.permanent, pm, sizeof(pm)), fz, d.clearResult, d.codesValid, (unsigned long)d.scanSeq);
    uint32_t sig = 2166136261u;  // only when it changed (a reconnect republishes the same)
    for (const char* p = text; *p; p++) sig = (sig ^ (uint8_t)*p) * 16777619u;
    if (sig == _logDiag) return;
    _logDiag = sig;
    gVehicleLog.record('C', text);
}

// a source without a speed (most motorcycle ECUs, a K-Line Wave) takes the phone's GPS speed (wiki 18)
void Vehicle::fillSpeedFromPhone(Snapshot& s, uint32_t now) {
    if ((int32_t)(now - _gpsCheckAt) < 0) return;
    _gpsCheckAt = now + 200;
    const bool ours = _gpsAt && s.ch[kSpeed].at == _gpsAt;
    if (s.fresh(kSpeed, now) && !ours) {  // the source has its own
        _speedFromGps = false;
        return;
    }
    const phone::Gps g = gPhone.gps();
    if (!g.fresh(now)) {
        _speedFromGps = false;
        return;
    }
    s.set(kSpeed, g.speedKmh, g.at);
    _gpsAt = g.at ? g.at : 1;
    _speedFromGps = true;
}

String Vehicle::status() {
    char st[sizeof(_status)];
    portENTER_CRITICAL(&_mux);
    memcpy(st, _status, sizeof(st));
    portEXIT_CRITICAL(&_mux);
    return String(st);
}

String Vehicle::klineDump() {
    char d[sizeof(_dump)];
    portENTER_CRITICAL(&_mux);
    memcpy(d, _dump, sizeof(d));
    portEXIT_CRITICAL(&_mux);
    return String(d);
}

void Vehicle::publish(const Snapshot& s, const char* st) {
    if (!_sh) return;
    const uint32_t now = millis();
    bool healthNew = false;
    if (_hm && (uint32_t)(now - _healthAt) >= kHealthEveryMs) {  // the health analysis, in this task
        _healthAt = now;
        if (_tripReset) {
            _tripReset = false;
            _hm->resetTrip();
        }
        obd::Diag d;
        portENTER_CRITICAL(&_mux);
        d = _sh->diag;
        portEXIT_CRITICAL(&_mux);
        _hm->update(s, &d, now);
        healthNew = true;
    }
    portENTER_CRITICAL(&_mux);
    _sh->snap = s;
    _sh->speedFromGps = _speedFromGps;
    if (healthNew) _sh->health = _hm->health();
    if (st) {
        strncpy(_status, st, sizeof(_status) - 1);
        _status[sizeof(_status) - 1] = '\0';
    }
    portEXIT_CRITICAL(&_mux);
    if (gVehicleLog.active()) logTick(s, st, now, healthNew);
}

// T115: the status when it changes, the decoded values once a second, the health when its findings change
void Vehicle::logTick(const Snapshot& s, const char* st, uint32_t now, bool healthNew) {
    if (st && strcmp(st, _logStatus)) {
        snprintf(_logStatus, sizeof(_logStatus), "%s", st);
        gVehicleLog.record('S', st);
    }
    if ((uint32_t)(now - _logValuesAt) >= 1000) {
        _logValuesAt = now;
        char v[640];
        if (obdlog::formatValues(s, now, v, sizeof(v))) gVehicleLog.record('D', v);
    }
    if (!healthNew || !_hm) return;
    const Health& h = _hm->health();
    uint32_t sig = h.worst;
    for (uint8_t i = 0; i < h.n; i++) sig = sig * 31 + (uint32_t)h.issue[i].id * 4 + h.issue[i].sev;
    if (sig == _logHealth) return;
    _logHealth = sig;
    char text[400];
    int len = snprintf(text, sizeof(text), "%s", severityKey(h.worst));
    for (uint8_t i = 0; i < h.n && len < (int)sizeof(text) - 2; i++) {
        len += snprintf(text + len, sizeof(text) - len, "%s %s %s (%.1f)", i ? ";" : ":", severityKey(h.issue[i].sev),
                        findingKey(h.issue[i].id), h.issue[i].value);
    }
    gVehicleLog.record('H', text);
}

void Vehicle::setStatus(const char* st) {
    portENTER_CRITICAL(&_mux);
    strncpy(_status, st, sizeof(_status) - 1);
    _status[sizeof(_status) - 1] = '\0';
    portEXIT_CRITICAL(&_mux);
}

void Vehicle::describe(JsonObject out, bool full) {
    const Settings s = settings();
    const DashConfig cfg = dashConfig();
    const uint32_t now = millis();
    const Snapshot snap = snapshot();
    const obd::Diag d = diag();
    const Health h = health();
    out["source"] = sourceKey(s.source);
    out["source_name"] = sourceNameTh(s.source);
    out["status"] = status();
    out["kind"] = s.kind == Kind::Car ? "car" : "motorcycle";
    out["style"] = styleKey(s.style);
    if (simulated(s.source)) out["simulated"] = true;  // not a real vehicle
    if (s.source == SourceId::Replay) {  // T115: a recorded drive, not the car now
        JsonObject rj = out["replay"].to<JsonObject>();
        char name[sizeof(_replayName)];
        portENTER_CRITICAL(&_mux);
        memcpy(name, _replayName, sizeof(name));
        portEXIT_CRITICAL(&_mux);
        rj["file"] = (const char*)name;
        rj["at_s"] = _replayAt / 1000;
        rj["length_s"] = _replayLen / 1000;
        if (_replaySpeed != 1.0f) rj["speed"] = _replaySpeed;
    } else if (gVehicleLog.active()) {
        out["sd_log"] = "recording";
    }
    if (needsRestart()) out["needs_restart"] = "restart the board once to start Bluetooth";
    JsonObject r = out["readings"].to<JsonObject>();
    for (int i = 0; i < kChannelCount; i++) {
        if (!snap.fresh((Channel)i, now)) continue;
        r[channelKey((Channel)i)] = roundTo(snap.value((Channel)i), channelDecimals((Channel)i));
    }
    if (speedFromGps() && snap.fresh(kSpeed, now)) out["speed_from"] = "phone GPS";
    DashModel m;
    m.setConfig(cfg);
    const DashView& v = m.update(snap, now);
    if (v.gear >= 0 && !snap.fresh(kGear, now)) r["gear_estimated"] = v.gear;
    if (v.coolantLevel == Level::Cold) out["engine_cold"] = true;

    // ---- health: findings (most serious first), trends ----
    JsonObject hj = out["health"].to<JsonObject>();
    hj["overall"] = severityKey(h.worst);
    JsonArray f = hj["findings"].to<JsonArray>();
    for (uint8_t i = 0; i < h.n; i++) {
        JsonObject o = f.add<JsonObject>();
        o["what"] = findingKey(h.issue[i].id);
        o["level"] = severityKey(h.issue[i].sev);
        o["value"] = roundTo(h.issue[i].value, 1);
    }
    if (h.coolantRateValid && fabsf(h.coolantRate) >= 0.5f) hj["coolant_change_c_per_min"] = roundTo(h.coolantRate, 1);
    for (int b = 0; b < 2; b++) {
        if (h.trimValid[b]) hj[b ? "fuel_trim_b2_avg_pct" : "fuel_trim_b1_avg_pct"] = roundTo(h.trim[b], 1);
    }
    if (h.restValid) {
        hj["battery_engine_off_v"] = roundTo(h.restV, 2);
        hj["battery_charge_pct_approx"] = h.restPct;
    }
    if (h.startMinV > 0) hj["last_start_lowest_v"] = roundTo(h.startMinV, 1);

    // ---- trip ----
    const Trip& t = h.trip;
    if (t.km > 0.05f || t.runMin > 0.2f) {
        JsonObject tj = out["trip"].to<JsonObject>();
        tj["km"] = roundTo(t.km, 2);
        tj["engine_min"] = roundTo(t.runMin, 1);
        if (t.idleMin > 0.1f) tj["idle_min"] = roundTo(t.idleMin, 1);
        if (t.avgKmh() > 0) tj["avg_kmh"] = roundTo(t.avgKmh(), 1);
        tj["max_kmh"] = roundTo(t.maxKmh, 0);
        if (t.fuelFrom) {
            tj["fuel_l"] = roundTo(t.fuelL, 2);
            if (t.avgKmL() > 0) tj["km_per_l"] = roundTo(t.avgKmL(), 1);
            if (t.instKmL > 0) tj["km_per_l_now"] = roundTo(t.instKmL, 1);
            tj["fuel_lph_now"] = roundTo(t.lph, 2);
            tj["fuel_from"] = fuelFromKey(t.fuelFrom);  // map_estimate: from engine size, roughly
        }
    }

    // ---- trouble codes (OBD-II, simulator) ----
    if (d.available) {
        JsonObject dj = out["diagnosis"].to<JsonObject>();
        if (d.simulated) dj["simulated"] = true;
        if (d.ready.valid) dj["check_engine_light"] = d.ready.mil;
        if (!d.codesValid) {
            dj["codes"] = "not read yet";
        } else {
            JsonArray c = dj["codes"].to<JsonArray>();
            for (uint8_t i = 0; i < d.stored.n; i++) addCode(c, d.stored.code[i], "stored", full);
            for (uint8_t i = 0; i < d.pending.n; i++) addCode(c, d.pending.code[i], "pending", full);
            for (uint8_t i = 0; i < d.permanent.n; i++) {
                if (!d.stored.has(d.permanent.code[i])) addCode(c, d.permanent.code[i], "permanent", full);
            }
            dj["read_s_ago"] = (uint32_t)(now - d.codesAt) / 1000;
        }
        if (d.clearResult) {
            static const char* const kClear[] = {"", "cleared", "refused by the ECU (engine running?)", "no answer"};
            dj["last_clear"] = kClear[d.clearResult < 4 ? d.clearResult : 0];
        }
        if (full) {
            if (d.ready.valid && d.ready.total) {
                char rd[24];
                snprintf(rd, sizeof(rd), "%u of %u complete", d.ready.done, d.ready.total);
                dj["readiness"] = rd;
            }
            if (d.freeze.dtc) {
                JsonObject fz = dj["freeze_frame"].to<JsonObject>();
                char code[6];
                obd::formatDtc(d.freeze.dtc, code);
                fz["code"] = code;
                for (uint8_t i = 0; i < d.freeze.n; i++) {
                    const obd::Pid* p = obd::findPid(d.freeze.pid[i]);
                    if (p && p->ch < kChannelCount) fz[channelKey(p->ch)] = roundTo(d.freeze.value[i], channelDecimals(p->ch));
                }
            }
            if (d.vin[0]) dj["vin"] = d.vin;
            if (d.protocol) dj["protocol"] = obd::protocolName(d.protocol);
            if (d.supportedPids) dj["supported_pids"] = d.supportedPids;
            if (d.fuelType == 4) dj["fuel"] = "diesel";
        }
    }
    out["shift_rpm"] = cfg.shiftRpm;
    out["rpm_max"] = cfg.rpmMax;
}

bool Vehicle::takeAlert(String& msg, uint32_t now) {
    const SourceId src = source();
    if (src == SourceId::Off) return false;
    const bool sim = simulated(src), replay = src == SourceId::Replay;
    char buf[200];
    // a trouble code not said yet (once each per boot): the light alone tells the driver nothing. Not the
    // simulator's: they are always there, and opening the demo dash should not start with a warning
    const obd::Diag d = diag();
    if (d.available && d.codesValid && !d.simulated && !replay) {
        for (uint8_t i = 0; i < d.stored.n; i++) {
            const uint16_t c = d.stored.code[i];
            if (_announced.has(c)) continue;
            _announced.add(c);
            char code[6];
            obd::formatDtc(c, code);
            const obd::DtcInfo info = obd::describeDtc(c);
            snprintf(buf, sizeof(buf), "%sรหัสปัญหา %s = %s [%s, ความรุนแรง %u จาก 3]",
                     d.ready.mil ? "ไฟเช็คเครื่องยนต์ติด: " : "", code, info.text[0] ? info.text : info.system,
                     info.systemTh, info.severity);
            msg = buf;
            return true;
        }
    }
    const Health h = health();
    for (uint8_t i = 0; i < h.n; i++) {
        const Issue& is = h.issue[i];
        const uint16_t every = findingSpeakMin(is.id);
        if (!every || is.sev < kSevWarn) continue;
        if (is.id == Finding::Overheat && is.sev < kSevCrit) continue;  // 105-112 C: shown, said only when critical
        uint32_t& at = _alertAt[(int)is.id];
        if (at && (uint32_t)(now - at) < (uint32_t)every * 60000u) continue;
        at = now ? now : 1;
        switch (is.id) {
            case Finding::Overheat:
                snprintf(buf, sizeof(buf), "อุณหภูมิเครื่องยนต์ %.0f°C สูงเกิน ควรจอดพักเครื่อง", is.value);
                break;
            case Finding::OverheatRising:
                snprintf(buf, sizeof(buf), "อุณหภูมิเครื่องยนต์ขึ้นเร็ว %.1f°C ต่อนาที ใกล้ระดับเตือนแล้ว ควรสังเกตเกจและหาที่จอด",
                         is.value);
                break;
            case Finding::NotCharging:
                snprintf(buf, sizeof(buf), "แรงดันแบตเตอรี่ %.1f V ต่ำ ขณะเครื่องติด (ไดชาร์จอาจไม่ชาร์จ)", is.value);
                break;
            case Finding::Overcharge:
                snprintf(buf, sizeof(buf), "แรงดันแบตเตอรี่ %.1f V สูงเกิน (เรกูเลเตอร์อาจเสีย)", is.value);
                break;
            case Finding::LowBatteryParked:
                snprintf(buf, sizeof(buf), "แบตเตอรี่ %.2f V ตอนดับเครื่อง (ประจุราว %u%%) อาจสตาร์ทไม่ติด", is.value,
                         HealthMonitor::restPercent(is.value));
                break;
            case Finding::WeakStart:
                snprintf(buf, sizeof(buf), "ตอนสตาร์ทแรงดันแบตตกถึง %.1f V แบตเตอรี่อาจเริ่มเสื่อม", is.value);
                break;
            case Finding::LowFuel:
                snprintf(buf, sizeof(buf), "น้ำมันเหลือราว %.0f%% ของถัง ควรเติมเร็วๆ นี้", is.value);
                break;
            default:  // lean / rich
                snprintf(buf, sizeof(buf), "%s: กล่อง ECU ชดเชยน้ำมัน (fuel trim) %+.1f%% ควรตรวจหารอยรั่วอากาศ/ระบบน้ำมัน",
                         findingTh(is.id), is.value);
                break;
        }
        msg = buf;
        if (sim) msg += " (ข้อมูลจำลอง)";
        if (replay) msg += " (ข้อมูลย้อนหลังจาก log ไม่ใช่รถตอนนี้)";
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- task

void Vehicle::startTask() {
    if (_task) return;
    // PSRAM stack for every source (T113: BLE too - BleElmLink runs BLEDevice::init, which reads NVS, on a short-lived
    // task with an internal stack, and a found adapter is saved by service()): 6 KB of internal RAM back
    // 8 KB (T115: the SD log formats a record on it, a replay keeps its index)
    const bool ok = startPsramTask(taskEntry, "vehicle", 8192, this, 3, &_task, 0);
    if (!ok) {
        _task = nullptr;
        setStatus("NO TASK");
    }
}

void Vehicle::taskEntry(void* self) { static_cast<Vehicle*>(self)->loop(); }

void Vehicle::loop() {
    for (;;) {
        _restart = false;
        const Settings s = settings();
        Snapshot empty;
        if (_hm) _hm->reset(healthConfig());  // a new source: a new trip, no findings from the old one
        _lastInSeq = UINT32_MAX;
        publishDiag(obd::Diag());
        _scanReq = _clearReq = false;
        _speedFromGps = false;
        publish(empty, nullptr);
        switch (s.source) {
            case SourceId::Off:
                setStatus("OFF");
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // until another source is chosen
                break;
            case SourceId::SimAuto:
            case SourceId::SimTouch:
                runSim(s.source == SourceId::SimTouch);
                break;
            case SourceId::HondaK: {
                UartLink link(hondak::kBaud, true);
                runKline(link);
                link.close();
                break;
            }
            case SourceId::ObdUart: {
                UartLink link(s.elmBaud);
                runElm(link, ElmKind::Uart);
                link.close();
                break;
            }
#if VEHICLE_BLE
            case SourceId::ObdBle: {
                BleElmLink link(s.bleAddr, s.bleType);
                runElm(link, ElmKind::Ble);
                vehicleBleShutdown();  // another source: Bluetooth off
                break;
            }
#endif
            case SourceId::PhoneGps:
                runPhone();
                break;
            case SourceId::Replay:
                runReplay();
                break;
            case SourceId::SerialText: {
                UartLink link(s.textBaud);
                runText(link);
                link.close();
                break;
            }
            default:
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
        }
    }
}

void Vehicle::runSim(bool touch) {
    SimEngine sim;
    sim.reset(settings().kind);
    sim.setAutoDrive(!touch);
    Snapshot snap;
    obd::Diag d;
    uint32_t last = millis();
    sim.diag(d);
    d.codesAt = last;
    d.scanSeq = 1;
    publishDiag(d);
    uint32_t scanAt = 0;  // a scan / clear asked for: "done" at this time
    while (!_restart) {
        vTaskDelay(pdMS_TO_TICKS(20));
        const uint32_t now = millis();
        if (_clearReq) {  // the simulator clears with its engine running (a real car must be switched off)
            _clearReq = false;
            sim.clearCodes();
            d.clearResult = obd::kClearOk;
            scanAt = now + kSimScanMs;
        }
        if (_scanReq) {
            _scanReq = false;
            if (!scanAt) scanAt = now + kSimScanMs;
        }
        if (scanAt && (int32_t)(now - scanAt) >= 0) {
            scanAt = 0;
            sim.diag(d);
            d.codesAt = now;
            d.scanSeq++;
            publishDiag(d);
        }
        if (touch) sim.setThrottle(_touch > 0 ? _touch : -0.35f);  // released: brake gently to a stop
        sim.step((now - last) / 1000.0f);
        last = now;
        sim.write(snap, now);
        publish(snap, touch ? "SIM TOUCH" : "SIM");
    }
}

void Vehicle::runKline(VehicleLink& link) {
    UartLink& uart = static_cast<UartLink&>(link);
    hondak::Session k;
    {
        Guard g(_lock);
        k.setLayout(_layout);
    }
    Snapshot snap;
    uart.open();
    k.restart(millis());
    uint8_t buf[64];
    uint32_t frames = 0;
    logStart();  // T115: the tables as hex (F) and the values (D) on the SD card
    while (!_restart) {
        const uint32_t now = millis();
        const hondak::Session::Action a = k.poll(now);
        if (a.kind == hondak::Session::Action::WakePulse) {
            uart.wakePulse(hondak::kWakeLowMs, hondak::kWakeHighMs);
            k.wakeDone(millis());
        } else if (a.kind == hondak::Session::Action::Send) {
            uart.write(a.bytes, a.n);
        }
        const size_t n = uart.read(buf, sizeof(buf));
        if (n) k.feed(buf, n, snap, millis());
        if (k.frames() != frames) {
            frames = k.frames();
            portENTER_CRITICAL(&_mux);
            strncpy(_dump, k.lastDump(), sizeof(_dump) - 1);
            portEXIT_CRITICAL(&_mux);
            gVehicleLog.record('F', k.lastDump());
        }
        fillSpeedFromPhone(snap, millis());  // Wave-type ECUs know no speed: the phone's GPS
        publish(snap, k.status());
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    gVehicleLog.stop();
}

void Vehicle::logStart() {
    const Settings s = settings();
    gVehicleLog.start(sourceKey(s.source));
    gVehicleLog.recordf('E', "source %s (%s), %s, firmware %s, internal RAM %u KB free", sourceKey(s.source),
                        sourceNameTh(s.source), s.kind == Kind::Car ? "car" : "motorcycle", FIRMWARE_VERSION,
                        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    _logStatus[0] = '\0';
    _logValuesAt = 0;
    _logHealth = UINT32_MAX;
    _logDiag = 0;
}

void Vehicle::runElm(VehicleLink& link, ElmKind kind) {
    const bool ble = kind == ElmKind::Ble, uart = kind == ElmKind::Uart, replay = kind == ElmKind::Replay;
    Elm327 elm;
    const Settings s = settings();
    elm.setProtocolHint(replay ? 0 : s.protocol);
    if (!replay) elm.setTrace(&gVehicleLog);  // T115: every command / reply into the SD log (while a file is open)
    Snapshot snap;
    char buf[128];
    // wired adapters come set to 38400 (ELM327) / 9600 (some clones, STN) / 115200 (OBDLink): tried in turn while silent
    static const uint32_t kBauds[] = {38400, 9600, 115200, 57600, 230400, 500000};
    uint32_t baud = s.elmBaud, resets = 0, diagVer = 0, noCarSince = 0;
    int baudTry = -1;
    bool baudLearned = false;
    if (uart) logStart();  // wired: the whole run (Bluetooth: each time the dash switches it on)
    while (!_restart) {
#if VEHICLE_BLE
        // T114: Bluetooth follows the dash screen - on while it shows (and kBleOffAfterMs after, e.g. its settings
        // page), off otherwise: its ~25 KB of internal RAM go back to the assistant
        if (ble) {
            const uint32_t seen = _visibleAt;
            // signed: the UI stamps `now | 1`, which can be 1 ms ahead of this task's millis() (unsigned = 49 days ago)
            if (!seen || (int32_t)(millis() - seen) >= (int32_t)kBleOffAfterMs) {
                if (link.isOpen() || vehicleBleRunning()) {
                    gVehicleLog.record('E', "dash closed - Bluetooth off");
                    link.close();
                    vehicleBleShutdown();
                    Serial.printf("[Vehicle] BLE: dash closed - Bluetooth off (%u KB internal RAM free)\n",
                                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
                    gVehicleLog.stop();
                }
                publish(snap, "BT OFF");
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(300));  // noteDashVisible() wakes it sooner
                continue;
            }
            if (!vehicleBleRunning() && !gVehicleLog.active()) {  // the dash just opened: a new log of this drive
                logStart();
                gVehicleLog.record('E', "dash open - Bluetooth on");
            }
        }
#endif
        if (!link.isOpen()) {
            setStatus(link.status());
            if (!link.open()) {
                setStatus(link.status());
                // BLE "restart needed" waits for a source change; anything else tries again in a while. A BLE scan
                // shares the radio with Wi-Fi (the assistant's voice): not more often than every 15 s
                const bool waitForever = !strcmp(link.status(), "BT RESTART") || !strcmp(link.status(), "BT FAIL");
                ulTaskNotifyTake(pdTRUE, waitForever ? portMAX_DELAY : pdMS_TO_TICKS(ble ? 15000 : 5000));
                continue;
            }
#if VEHICLE_BLE
            if (ble) {
                BleElmLink& b = static_cast<BleElmLink&>(link);
                if (b.foundAddress()[0]) {  // a scan found it: connect straight away next time
                    {
                        Guard g(_lock);
                        snprintf(_s.bleAddr, sizeof(_s.bleAddr), "%s", b.foundAddress());
                        _s.bleType = b.foundType();
                    }
                    _learnedBle = true;  // saved by service(): this task's stack is PSRAM now (T113)
                }
            }
#endif
            elm.restart(millis());
        }
        if (_scanReq) {
            _scanReq = false;
            elm.requestScan();
        }
        if (_clearReq) {
            _clearReq = false;
            gVehicleLog.record('E', "clear codes requested");
            elm.requestClear();
        }
        elm.setFocus((Elm327::Focus)focus());
        const uint32_t now = millis();
        if (const char* cmd = elm.poll(now)) {
            char line[16];
            const int len = snprintf(line, sizeof(line), "%s\r", cmd);
            link.write((const uint8_t*)line, (size_t)len);
        }
        const size_t n = link.read((uint8_t*)buf, sizeof(buf));
        if (n) elm.feed(buf, n, snap, millis());
        if (elm.diagVersion() != diagVer) {
            diagVer = elm.diagVersion();
            const obd::Diag& d = elm.diag();
            publishDiag(d);
            // tried first next time (not from a replay: maybe another car)
            if (!replay && d.protocol && d.protocol != s.protocol && d.protocol <= 9) _learnedProto = d.protocol;
            if (d.fuelType == 4 && _hm && !_hm->config().diesel) {  // a diesel: its air flow does not give the fuel
                HealthConfig c = _hm->config();
                c.diesel = true;
                _hm->setConfig(c);
            }
        }
        if (uart) {
            if (elm.silentResets() != resets) {  // the adapter never answered: another baud rate
                resets = elm.silentResets();
                baudTry = (baudTry + 1) % (int)(sizeof(kBauds) / sizeof(kBauds[0]));
                if (kBauds[baudTry] == baud) baudTry = (baudTry + 1) % (int)(sizeof(kBauds) / sizeof(kBauds[0]));
                Serial.printf("[Vehicle] ELM327 silent at %lu baud - trying %lu\n", (unsigned long)baud,
                              (unsigned long)kBauds[baudTry]);
                gVehicleLog.recordf('E', "ELM327 silent at %lu baud - trying %lu", (unsigned long)baud,
                                    (unsigned long)kBauds[baudTry]);
                baud = kBauds[baudTry];
                static_cast<UartLink&>(link).setBaud(baud);  // reopened (and the ELM restarted) above
            } else if (elm.answered() && !baudLearned) {
                baudLearned = true;
                if (baud != s.elmBaud) _learnedBaud = baud;
            }
            // parked with the board powered: the file closes after 10 minutes without a car, a new one when it answers
            const bool car = elm.phase() == Elm327::Phase::Discover || elm.phase() == Elm327::Phase::Poll;
            if (car) {
                noCarSince = 0;
                if (!gVehicleLog.active()) logStart();
            } else if (!noCarSince) {
                noCarSince = now ? now : 1;
            } else if (gVehicleLog.active() && (uint32_t)(now - noCarSince) > 10 * 60 * 1000) {
                gVehicleLog.record('E', "no ECU for 10 min - file closed");
                gVehicleLog.stop();
            }
        }
        if (replay) {
            _replayAt = static_cast<ReplayLink&>(link).position();
        } else {
            fillSpeedFromPhone(snap, millis());
        }
        publish(snap, elm.status());
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    link.close();
    if (!replay) gVehicleLog.stop();
}

// T115: a drive recorded on the SD card. With the adapter's traffic in it: through the same driver (the recorded
// answers, matched by command and time); otherwise (K-Line, text lines) the recorded values. Loops at the end.
void Vehicle::runReplay() {
    const Settings s = settings();
    char path[128];
    if (!gVehicleLog.path(s.replayFile[0] ? s.replayFile : "latest", path, sizeof(path))) {
        setStatus(gVehicleLog.available() ? "NO LOG" : "NO SD CARD");
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // until another source / file is chosen
        return;
    }
    FILE* f = fopen(path, "rb");
    long size = 0;
    if (f && fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    constexpr long kMaxLoad = 4L * 1024 * 1024;  // a log rotates at 2 MB; an older or edited one is cut here
    if (size > kMaxLoad) size = kMaxLoad;
    char* data = f && size > 0 ? (char*)psramMalloc((size_t)size + 1) : nullptr;
    size_t n = 0;
    if (data) {
        fseek(f, 0, SEEK_SET);
        n = fread(data, 1, (size_t)size, f);
        data[n] = '\0';
    }
    if (f) fclose(f);
    obdlog::Replay rp;
    if (!data || !rp.load(data, n)) {
        setStatus(data ? "BAD LOG" : "NO MEMORY");
        if (data) psramFree(data);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        return;
    }
    portENTER_CRITICAL(&_mux);
    snprintf(_replayName, sizeof(_replayName), "%s", strrchr(path, '/') + 1);
    portEXIT_CRITICAL(&_mux);
    _replayLen = rp.duration();
    _replayAt = 0;
    Serial.printf("[Vehicle] Replay %s: %.1f s, %u records, %s, x%.1f\n", strrchr(path, '/') + 1,
                  rp.duration() / 1000.0f, (unsigned)rp.records(),
                  rp.hasAdapterTrace() ? "through the ELM327 driver" : "recorded values", _replaySpeed);
    if (rp.hasAdapterTrace()) {
        ReplayLink link(&rp, _replaySpeed);
        runElm(link, ElmKind::Replay);
    } else {
        obdlog::ReplayAdapter clock;
        clock.begin(&rp, millis(), _replaySpeed);
        Snapshot snap;
        while (!_restart) {
            const uint32_t now = millis();
            const uint32_t t = clock.logTime(now);
            rp.values(t, snap, now);
            _replayAt = t - rp.start();
            publish(snap, "REPLAY");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    psramFree(data);
}

void Vehicle::runText(VehicleLink& link) {
    Snapshot snap;
    char line[160];
    size_t len = 0;
    uint8_t buf[64];
    link.open();
    uint32_t lastLine = 0;
    logStart();  // T115: each line as received (L) on the SD card
    while (!_restart) {
        const size_t n = link.read(buf, sizeof(buf));
        for (size_t i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                if (len && parseTextLine(line, snap, millis())) lastLine = millis();
                if (len) gVehicleLog.record('L', line);
                len = 0;
            } else if (len + 1 < sizeof(line)) {
                line[len++] = c;
            }
        }
        fillSpeedFromPhone(snap, millis());
        publish(snap, lastLine && millis() - lastLine < kStaleMs ? "LIVE" : "WAIT DATA");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    gVehicleLog.stop();
}

// the phone's GPS (wiki 18): speed only - the dash shows "--" for what a GPS cannot know
void Vehicle::runPhone() {
    Snapshot snap;
    while (!_restart) {
        const uint32_t now = millis();
        const phone::Gps g = gPhone.gps();
        if (g.fresh(now)) snap.set(kSpeed, g.speedKmh, g.at);
        publish(snap, g.fresh(now) ? "GPS" : gPhone.connected() ? "NO FIX" : "NO PHONE");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
