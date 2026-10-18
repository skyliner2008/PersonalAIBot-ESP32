/**
 * @file Vehicle.cpp
 * @brief Vehicle dash back end (see Vehicle.h)
 */

#include "Vehicle.h"

#include <Preferences.h>

#include "../system/Peripherals.h"
#include "../system/PsramTask.h"
#include "../util/JsonAlloc.h"
#include "../util/JsonFile.h"
#include "Elm327.h"
#include "SimEngine.h"
#include "VehicleLink.h"

Vehicle gVehicle;

using namespace vehicle;

namespace {

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m); }
};

constexpr uint32_t kAlertRepeatMs = 5 * 60 * 1000;
constexpr uint32_t kAlertHoldMs = 5000;  // a reading must stay bad this long (no alert for one glitch)

}  // namespace

bool Vehicle::bleKeptAtBoot() { return vehicleBleKeptAtBoot(); }

// ---------------------------------------------------------------- settings

void Vehicle::begin(const char* agentDir) {
    _lock = xSemaphoreCreateMutex();
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
        p.end();
    }
    // agent/vehicle.json: {"kline": {"table": 17, "rpm": 0, "coolant": 5, ...}} - which byte holds what on your bike
    if (_dir[0]) {
        JsonDocument doc(JsonPsramAllocator::instance());
        const String path = String(_dir) + "/vehicle.json";
        if (jsonfile::load(path.c_str(), doc)) {
            hondak::parseLayout(doc["kline"].as<JsonObjectConst>(), _layout);
            Serial.printf("[Vehicle] %s: K-Line table 0x%02X\n", path.c_str(), _layout.table);
        }
    }
    if (usesUart(_s.source) && (gPeriph.usesPin(UartLink::kTx) || gPeriph.usesPin(UartLink::kRx))) {
        Serial.println("[Vehicle] GPIO 43/44 belong to a device in devices.json - source turned off");
        _s.source = SourceId::Off;
    }
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
    p.end();
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

bool Vehicle::needsRestart() { return source() == SourceId::ObdBle && !bleKeptAtBoot(); }

// ---------------------------------------------------------------- shared readings

Snapshot Vehicle::snapshot() {
    Snapshot s;
    portENTER_CRITICAL(&_mux);
    s = _shared;
    portEXIT_CRITICAL(&_mux);
    return s;
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
    portENTER_CRITICAL(&_mux);
    _shared = s;
    if (st) {
        strncpy(_status, st, sizeof(_status) - 1);
        _status[sizeof(_status) - 1] = '\0';
    }
    portEXIT_CRITICAL(&_mux);
}

void Vehicle::setStatus(const char* st) {
    portENTER_CRITICAL(&_mux);
    strncpy(_status, st, sizeof(_status) - 1);
    _status[sizeof(_status) - 1] = '\0';
    portEXIT_CRITICAL(&_mux);
}

void Vehicle::describe(JsonObject out) {
    const Settings s = settings();
    const DashConfig cfg = dashConfig();
    const uint32_t now = millis();
    const Snapshot snap = snapshot();
    out["source"] = sourceKey(s.source);
    out["source_name"] = sourceNameTh(s.source);
    out["status"] = status();
    out["kind"] = s.kind == Kind::Car ? "car" : "motorcycle";
    out["style"] = styleKey(s.style);
    if (s.source == SourceId::SimAuto || s.source == SourceId::SimTouch) out["simulated"] = true;  // not a real vehicle
    if (needsRestart()) out["needs_restart"] = "restart the board once to start Bluetooth";
    JsonObject r = out["readings"].to<JsonObject>();
    for (int i = 0; i < kChannelCount; i++) {
        if (!snap.fresh((Channel)i, now)) continue;
        const float v = snap.value((Channel)i);
        r[channelKey((Channel)i)] = i == kBattery ? roundf(v * 10) / 10 : roundf(v);
    }
    DashModel m;
    m.setConfig(cfg);
    const DashView& v = m.update(snap, now);
    if (v.gear >= 0 && !snap.fresh(kGear, now)) r["gear_estimated"] = v.gear;
    JsonArray w = out["warnings"].to<JsonArray>();
    if (v.coolantLevel == Level::Crit) w.add("engine temperature critical");
    else if (v.coolantLevel == Level::Warn) w.add("engine temperature high");
    else if (v.coolantLevel == Level::Cold) w.add("engine not warmed up yet");
    if (v.batteryLevel == Level::Crit) w.add("battery voltage too high (regulator?)");
    else if (v.batteryLevel == Level::Warn) w.add("battery voltage low");
    out["shift_rpm"] = cfg.shiftRpm;
    out["rpm_max"] = cfg.rpmMax;
}

bool Vehicle::takeAlert(String& msg, uint32_t now) {
    const SourceId src = source();
    if (src == SourceId::Off) return false;
    const Snapshot snap = snapshot();
    _alertModel.setConfig(dashConfig());
    const DashView& v = _alertModel.update(snap, now);
    const bool sim = src == SourceId::SimAuto || src == SourceId::SimTouch;
    const bool bad[3] = {v.coolantLevel == Level::Crit, v.batteryLevel == Level::Warn && v.rpmValid && v.rpm > 400,
                         v.batteryLevel == Level::Crit};
    for (int i = 0; i < 3; i++) {
        if (!bad[i]) {
            _alertActive[i] = false;
            continue;
        }
        if (!_alertActive[i]) {  // just went bad: wait kAlertHoldMs
            _alertActive[i] = true;
            _alertAt[i] = now - kAlertRepeatMs + kAlertHoldMs;
            continue;
        }
        if ((uint32_t)(now - _alertAt[i]) < kAlertRepeatMs) continue;
        _alertAt[i] = now;
        char buf[160];
        if (i == 0) snprintf(buf, sizeof(buf), "อุณหภูมิเครื่องยนต์ %.0f°C สูงเกิน ควรจอดพักเครื่อง", v.coolant);
        else if (i == 1) snprintf(buf, sizeof(buf), "แรงดันแบตเตอรี่ %.1f V ต่ำ ขณะเครื่องติด (ไดชาร์จอาจไม่ชาร์จ)", v.battery);
        else snprintf(buf, sizeof(buf), "แรงดันแบตเตอรี่ %.1f V สูงเกิน (เรกูเลเตอร์อาจเสีย)", v.battery);
        msg = buf;
        if (sim) msg += " (ข้อมูลจำลอง)";
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- task

void Vehicle::startTask() {
    if (_task) return;
    // BLE calls (NimBLE init, NVS bonding reads) need an internal stack; every other source is fine in PSRAM
    const bool ble = _s.source == SourceId::ObdBle && bleKeptAtBoot();
    const bool ok = ble ? xTaskCreatePinnedToCore(taskEntry, "vehicle", 6144, this, 3, &_task, 0) == pdPASS
                        : startPsramTask(taskEntry, "vehicle", 6144, this, 3, &_task, 0);
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
                runElm(link, false);
                link.close();
                break;
            }
#if VEHICLE_BLE
            case SourceId::ObdBle: {
                BleElmLink link(s.bleAddr, s.bleType);
                runElm(link, true);
                break;
            }
#endif
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
    uint32_t last = millis();
    while (!_restart) {
        vTaskDelay(pdMS_TO_TICKS(20));
        const uint32_t now = millis();
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
        }
        publish(snap, k.status());
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void Vehicle::runElm(VehicleLink& link, bool ble) {
    Elm327 elm;
    Snapshot snap;
    char buf[128];
    while (!_restart) {
        if (!link.isOpen()) {
            setStatus(link.status());
            if (!link.open()) {
                setStatus(link.status());
                // BLE "restart needed" waits for a source change; anything else tries again in a while. A BLE scan
                // shares the radio with Wi-Fi (the assistant's voice): not more often than every 15 s
                const bool waitForever = !strcmp(link.status(), "BT RESTART");
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
                    save();  // this task has an internal stack when it runs BLE
                }
            }
#endif
            elm.restart(millis());
        }
        const uint32_t now = millis();
        if (const char* cmd = elm.poll(now)) {
            char line[16];
            const int len = snprintf(line, sizeof(line), "%s\r", cmd);
            link.write((const uint8_t*)line, (size_t)len);
        }
        const size_t n = link.read((uint8_t*)buf, sizeof(buf));
        if (n) elm.feed(buf, n, snap, millis());
        publish(snap, elm.status());
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    link.close();
}

void Vehicle::runText(VehicleLink& link) {
    Snapshot snap;
    char line[160];
    size_t len = 0;
    uint8_t buf[64];
    link.open();
    uint32_t lastLine = 0;
    while (!_restart) {
        const size_t n = link.read(buf, sizeof(buf));
        for (size_t i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                if (len && parseTextLine(line, snap, millis())) lastLine = millis();
                len = 0;
            } else if (len + 1 < sizeof(line)) {
                line[len++] = c;
            }
        }
        publish(snap, lastLine && millis() - lastLine < kStaleMs ? "LIVE" : "WAIT DATA");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
