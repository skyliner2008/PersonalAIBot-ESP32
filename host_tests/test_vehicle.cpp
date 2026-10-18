/**
 * @file test_vehicle.cpp
 * @brief Host tests: vehicle dash - readings, dash model, simulator, ELM327 and Honda K-Line protocols
 */

#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

#include <string>

#include "test_framework.h"
#include "vehicle/DashModel.h"
#include "vehicle/Elm327.h"
#include "vehicle/HondaKline.h"
#include "vehicle/SimEngine.h"
#include "vehicle/VehicleData.h"

using namespace vehicle;

TEST(vehicle_readings_and_text_lines) {
    Snapshot s;
    CHECK(!s.fresh(kRpm, 1000));
    s.set(kRpm, 3200, 1000);
    CHECK(s.fresh(kRpm, 1000 + kStaleMs));
    CHECK(!s.fresh(kRpm, 1000 + kStaleMs + 1));  // old data shows "--", never a frozen value
    s.set(kSpeed, 50, 0xFFFFFF00u);
    CHECK(s.fresh(kSpeed, 0x00000100u));  // across the millis() wrap

    Snapshot t;
    CHECK_EQ(parseTextLine("rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3", t, 5), 6);
    CHECK(fabsf(t.value(kRpm) - 3200) < 0.1f && fabsf(t.value(kBattery) - 13.9f) < 0.01f);
    CHECK_EQ((int)t.value(kGear), 3);
    Snapshot j;
    CHECK_EQ(parseTextLine("{\"rpm\":2500,\"speed_kmh\":40.5,\"coolant\":91,\"unknown\":7}", j, 5), 3);
    CHECK(fabsf(j.value(kSpeed) - 40.5f) < 0.01f);
    Snapshot c;
    CHECK_EQ(parseTextLine("RPM:1800, TPS:12", c, 5), 2);  // any case, ':' and commas
    CHECK_EQ(parseTextLine("hello world", c, 5), 0);
    CHECK_EQ(parseTextLine("rpm=", c, 5), 0);
    CHECK_EQ(parseTextLine("", c, 5), 0);
    CHECK_EQ(parseTextLine(nullptr, c, 5), 0);

    SourceId id;
    CHECK(parseSource("kline", id) && id == SourceId::HondaK);
    CHECK(parseSource("OBD_BLE", id) && id == SourceId::ObdBle);
    CHECK(parseSource("honda", id) && id == SourceId::HondaK);
    CHECK(!parseSource("mt5", id));
    for (uint8_t i = 0; i < (uint8_t)SourceId::Count; i++) {
        CHECK(parseSource(sourceKey((SourceId)i), id) && id == (SourceId)i);  // every key reads back
    }
    CHECK(usesUart(SourceId::HondaK) && usesUart(SourceId::ObdUart) && !usesUart(SourceId::ObdBle));
}

TEST(vehicle_dash_model) {
    DashModel m;
    DashConfig cfg = defaultConfig(Kind::Car);
    m.setConfig(cfg);
    Snapshot s;
    uint32_t now = 1000;
    const DashView& v0 = m.update(s, now);
    CHECK(!v0.live && !v0.rpmValid && v0.gear == -1);

    // 3rd gear at 60 km/h: rpm = km/h * 1000 / 60 / circ * ratio * final
    const float rpm3 = 60.0f * 1000 / 60 / cfg.tireCircM * cfg.ratio[2] * cfg.finalDrive;
    CHECK_EQ(DashModel::estimateGear(cfg, rpm3, 60), 3);
    CHECK_EQ(DashModel::estimateGear(cfg, rpm3 * 1.05f, 60), 3);  // within the tolerance
    CHECK_EQ(DashModel::estimateGear(cfg, 900, 0), 0);            // standing at idle = N
    CHECK_EQ(DashModel::estimateGear(cfg, 3000, 0), -1);          // revving while standing: unknown
    CHECK_EQ(DashModel::estimateGear(cfg, 5000, 20), 0);          // clutch slipping: no gear fits
    CHECK_EQ(DashModel::estimateGear(defaultConfig(Kind::Motorcycle), 6000, 50), -1);  // bikes: off by default

    for (int i = 0; i < 40; i++) {  // 1 s of steady data at 25 Hz
        now += 40;
        s.set(kRpm, rpm3, now);
        s.set(kSpeed, 60, now);
        m.update(s, now);
    }
    const DashView& v = m.view();
    CHECK(v.live && v.rpmValid && fabsf(v.rpm - rpm3) < 30);
    CHECK_EQ(v.gear, 3);
    CHECK(v.gearEstimated);
    CHECK(!v.shift);

    s.set(kRpm, 6400, now);
    now += 40;
    m.update(s, now);
    CHECK(m.view().shift);
    CHECK(m.view().peakRpm >= 6400);
    bool on = false, off = false;
    for (int i = 0; i < 20; i++) {  // the shift light flashes
        now += 20;
        s.set(kRpm, 6400, now);
        m.update(s, now);
        on |= m.view().shiftOn;
        off |= !m.view().shiftOn;
    }
    CHECK(on && off);
    m.resetPeak();
    s.set(kRpm, 2000, now);
    m.update(s, now);
    CHECK(m.view().peakRpm < 2100);

    // warning levels
    s.set(kCoolant, 50, now);
    CHECK(m.update(s, now).coolantLevel == Level::Cold);
    s.set(kCoolant, 90, now);
    CHECK(m.update(s, now).coolantLevel == Level::Normal);
    s.set(kCoolant, 107, now);
    CHECK(m.update(s, now).coolantLevel == Level::Warn);
    s.set(kCoolant, 115, now);
    CHECK(m.update(s, now).coolantLevel == Level::Crit);
    s.set(kBattery, 11.2f, now);  // engine running (2000 rpm) and not charging
    CHECK(m.update(s, now).batteryLevel == Level::Warn);
    s.set(kBattery, 14.1f, now);
    CHECK(m.update(s, now).batteryLevel == Level::Normal);
    s.set(kBattery, 15.8f, now);
    CHECK(m.update(s, now).batteryLevel == Level::Crit);
    s.set(kGear, 4, now);  // a source that knows the gear wins over the estimate
    CHECK_EQ(m.update(s, now).gear, 4);

    // the data stops: every value goes stale
    now += kStaleMs + 100;
    const DashView& gone = m.update(s, now);
    CHECK(!gone.live && !gone.rpmValid && gone.coolantLevel == Level::None);
}

TEST(vehicle_simulator) {
    for (int k = 0; k < 2; k++) {
        const Kind kind = k ? Kind::Motorcycle : Kind::Car;
        const DashConfig cfg = defaultConfig(kind);
        SimEngine sim;
        sim.reset(kind);
        sim.setAutoDrive(true);
        float maxRpm = 0, maxSpeed = 0, minBatt = 99;
        int maxGear = 0;
        bool stopped = false, inRange = true;
        for (int i = 0; i < 600 * 50; i++) {  // 10 minutes at 50 Hz
            sim.step(0.02f);
            maxRpm = fmaxf(maxRpm, sim.rpm());
            maxSpeed = fmaxf(maxSpeed, sim.speedKmh());
            minBatt = fminf(minBatt, sim.battery());
            if (sim.gear() > maxGear) maxGear = sim.gear();
            if (sim.time() > 100 && fmodf(sim.time(), 60.0f) > 59.5f && sim.speedKmh() < 1) stopped = true;
            inRange &= sim.rpm() >= 0 && sim.rpm() < cfg.rpmMax + 500 && sim.speedKmh() >= 0 && sim.speedKmh() < 250;
        }
        CHECK(inRange);
        printf("    sim %s: max %.0f rpm, %.0f km/h, gear %d, coolant %.1f C, batt %.2f V (min %.2f)\n",
               k ? "bike" : "car", maxRpm, maxSpeed, maxGear, sim.coolant(), sim.battery(), minBatt);
        CHECK(maxRpm >= cfg.shiftRpm);  // the script pulls through the shift light
        CHECK(maxGear >= 3);            // and shifts up by itself
        CHECK(maxSpeed > 50);
        CHECK(stopped);                 // and brakes to a stop at the end of each loop
        CHECK(minBatt < 11.5f);         // the starter dip at the start
        CHECK(sim.battery() > 13.0f);   // charging while running
        CHECK(sim.coolant() > 70 && sim.coolant() < 120);  // warmed up, not boiling

        Snapshot s;
        sim.write(s, 77);
        CHECK(s.fresh(kRpm, 77) && s.fresh(kCoolant, 77) && s.fresh(kThrottle, 77));
    }
    // touch: no throttle = idle and standing, full throttle = moving
    SimEngine t;
    t.reset(Kind::Car);
    t.setAutoDrive(false);
    for (int i = 0; i < 200; i++) t.step(0.02f);
    CHECK(t.speedKmh() < 0.5f && fabsf(t.rpm() - 800) < 50);
    t.setThrottle(1.0f);
    for (int i = 0; i < 250; i++) t.step(0.02f);
    CHECK(t.speedKmh() > 30);
    t.setThrottle(-1.0f);  // brake
    for (int i = 0; i < 500; i++) t.step(0.02f);
    CHECK(t.speedKmh() < 0.5f);
}

namespace {
// a scripted ELM327: what a real v1.5 clone answers (echo on until ATE0, "\r\r>" after each reply)
struct FakeElm {
    bool echo = true;
    bool carOn = true;
    std::string reply(const std::string& cmd) {
        std::string r = echo ? cmd + "\r" : "";
        if (cmd == "ATZ") r += "\r\rELM327 v1.5\r\r>";
        else if (cmd == "ATE0") { echo = false; r += "OK\r\r>"; }
        else if (cmd.rfind("AT", 0) == 0 && cmd != "ATRV") r += "OK\r\r>";
        else if (cmd == "ATRV") r += "12.6V\r\r>";
        else if (!carOn) r += "SEARCHING...\rUNABLE TO CONNECT\r\r>";
        else if (cmd == "0100") r += "SEARCHING...\r4100BE3EB811\r\r>";  // 0C 0D 05 0F 11 0B supported
        else if (cmd == "010C") r += "410C1AF8\r\r>";                     // 1726 rpm
        else if (cmd == "010D") r += "410D3C\r\r>";                       // 60 km/h
        else if (cmd == "0105") r += "41057B\r\r>";                       // 83 C
        else if (cmd == "010F") r += "410F48\r\r>";                       // 32 C
        else if (cmd == "0111") r += "411140\r\r>";                       // 25 %
        else if (cmd == "010B") r += "410B21\r\r>";                       // 33 kPa
        else r += "NO DATA\r\r>";
        return r;
    }
};
}  // namespace

TEST(vehicle_elm327) {
    Snapshot s;
    CHECK(Elm327::decodeMode01("41 0C 1A F8", s, 1) && fabsf(s.value(kRpm) - 1726) < 0.1f);
    CHECK(Elm327::decodeMode01("410D3C", s, 1) && s.value(kSpeed) == 60);
    CHECK(Elm327::decodeMode01("41057B", s, 1) && s.value(kCoolant) == 83);
    CHECK(!Elm327::decodeMode01("NO DATA", s, 1));
    CHECK(!Elm327::decodeMode01("7F 01 12", s, 1));
    CHECK(!Elm327::decodeMode01("410C1A", s, 1));  // rpm needs two bytes
    float v = 0;
    CHECK(Elm327::parseVoltage("12.6V", v) && fabsf(v - 12.6f) < 0.01f);
    CHECK(Elm327::parseVoltage("14.1\n", v));
    CHECK(!Elm327::parseVoltage("?", v));

    FakeElm car;
    Elm327 elm;
    uint32_t now = 1000;
    elm.restart(now);
    int sent = 0;
    std::string order;
    bool sawAtrv = false;
    for (int i = 0; i < 400 && sent < 40; i++) {
        now += 10;
        if (const char* cmd = elm.poll(now)) {
            const std::string r = car.reply(cmd);
            sent++;
            if (!strcmp(cmd, "ATRV")) sawAtrv = true;
            if (!strncmp(cmd, "01", 2) && strcmp(cmd, "0100")) order += std::string(cmd + 2) + " ";
            // the adapter answers in pieces, like a BLE notification stream
            for (size_t k = 0; k < r.size(); k += 7) elm.feed(r.data() + k, r.size() - k < 7 ? r.size() - k : 7, s, now);
        }
    }
    CHECK(elm.phase() == Elm327::Phase::Poll);
    CHECK(!strcmp(elm.status(), "LIVE"));
    CHECK(!strcmp(elm.adapter(), "ELM327 v1.5"));
    CHECK(elm.supports(0x0C) && elm.supports(0x0D) && elm.supports(0x05) && elm.supports(0x0B));
    CHECK(!elm.supports(0x02));  // BE = 1011 1110: PID 02 is the 0 bit
    CHECK(s.fresh(kRpm, now) && s.fresh(kSpeed, now) && s.fresh(kCoolant, now) && s.fresh(kIntake, now));
    CHECK(s.fresh(kThrottle, now) && s.fresh(kMap, now) && s.fresh(kBattery, now));
    CHECK(sawAtrv);
    CHECK(order.find("0C 0D 0C 05 0C 0F") != std::string::npos);  // rpm every other request
    printf("    elm327 poll order: %.60s\n", order.c_str());

    // silence: the same command again (twice), then the init starts over and says so
    uint32_t t = now;
    const char* stuck = nullptr;
    for (int i = 0; i < 400 && !stuck; i++) stuck = elm.poll(t += 10);
    CHECK(stuck != nullptr);
    const std::string first = stuck ? stuck : "";
    const char* retry = elm.poll(t += 1501);
    CHECK(retry && first == retry);
    CHECK(elm.poll(t += 1501) != nullptr);
    CHECK(elm.poll(t += 1501) == nullptr);  // third miss: start over after a pause
    const char* reset = elm.poll(t += 1001);
    CHECK(reset && !strcmp(reset, "ATZ"));
    CHECK(!strcmp(elm.status(), "NO ADAPTER"));

    // ignition off: the search fails, it says NO ECU and asks again later
    FakeElm off;
    off.carOn = false;
    Elm327 e2;
    e2.restart(0);
    uint32_t n2 = 0;
    for (int i = 0; i < 300; i++) {
        n2 += 10;
        if (const char* cmd = e2.poll(n2)) {
            const std::string r = off.reply(cmd);
            e2.feed(r.data(), r.size(), s, n2);
        }
    }
    CHECK(e2.phase() == Elm327::Phase::NoEcu || e2.phase() == Elm327::Phase::Search);
    CHECK(e2.phase() != Elm327::Phase::Poll);
}

TEST(vehicle_honda_kline) {
    using namespace hondak;
    uint8_t req[5];
    tableRequest(0x11, req);
    const uint8_t want[5] = {0x72, 0x05, 0x71, 0x11, 0x07};
    CHECK(!memcmp(req, want, 5));
    const uint8_t hello[] = {0x72, 0x05, 0x00, 0xF0};
    CHECK_EQ(checksum(hello, 4), 0x99);

    // a table 0x11 reply: rpm 1520, coolant 78 C, intake 35 C, MAP 40 kPa, 13.8 V, 0 km/h
    uint8_t data[16] = {0x05, 0xF0, 0x1A, 0x00, 0x60, 78 + 40, 0x70, 35 + 40, 0x55, 40, 0, 0, 138, 0, 0, 0};
    uint8_t frame[4 + 16 + 1] = {0x02, (uint8_t)sizeof(frame), 0x71, 0x11};
    memcpy(frame + 4, data, 16);
    frame[sizeof(frame) - 1] = checksum(frame, sizeof(frame) - 1);

    Snapshot s;
    CHECK(decode(Layout(), data, sizeof(data), s, 10));
    CHECK(s.value(kRpm) == 1520 && s.value(kCoolant) == 78 && s.value(kIntake) == 35);
    CHECK(fabsf(s.value(kBattery) - 13.8f) < 0.01f && s.value(kMap) == 40 && s.value(kSpeed) == 0);
    CHECK(!s.fresh(kThrottle, 10));  // not read by default
    CHECK(!decode(Layout(), data, 1, s, 10));

    JsonDocument cfg;
    deserializeJson(cfg, R"({"table": 16, "speed": -1, "throttle": 3, "battery": 99})");
    Layout l;
    parseLayout(cfg.as<JsonObjectConst>(), l);
    CHECK_EQ(l.table, 0x10);
    CHECK_EQ(l.speed, -1);
    CHECK_EQ(l.throttle, 3);
    CHECK_EQ(l.battery, 12);  // out of range: default kept

    // the session: wake-up pulse -> FE 04 FF FF -> hello -> table polls; the transceiver echoes every byte
    Session k;
    uint32_t now = 0;
    k.restart(now);
    Session::Action a = k.poll(now);
    CHECK(a.kind == Session::Action::WakePulse);
    CHECK(!strcmp(k.status(), "K WAKE"));
    now += kWakeLowMs + kWakeHighMs;
    k.wakeDone(now);
    a = k.poll(now);
    CHECK(a.kind == Session::Action::Send && a.n == 4 && a.bytes[0] == 0xFE);
    k.feed(a.bytes, a.n, s, now);  // echo only, no answer
    CHECK(k.poll(now + 50).kind == Session::Action::None);  // waits before the hello
    now += 200;
    a = k.poll(now);
    CHECK(a.kind == Session::Action::Send && a.n == 5 && a.bytes[0] == 0x72 && a.bytes[2] == 0x00);
    const uint8_t helloEcho[] = {0x72, 0x05, 0x00, 0xF0, 0x99, 0x02, 0x04, 0x00, 0xFA};
    k.feed(helloEcho, sizeof(helloEcho), s, now + 20);
    CHECK(k.phase() == Session::Phase::Poll);
    Snapshot live;
    int polls = 0;
    for (int i = 0; i < 100; i++) {
        now += 10;
        a = k.poll(now);
        if (a.kind != Session::Action::Send) continue;
        polls++;
        CHECK(a.n == 5 && a.bytes[2] == 0x71 && a.bytes[3] == 0x11);
        uint8_t wire[5 + sizeof(frame)];
        memcpy(wire, a.bytes, 5);  // echo first
        memcpy(wire + 5, frame, sizeof(frame));
        k.feed(wire, 3, live, now + 5);  // in pieces
        k.feed(wire + 3, sizeof(wire) - 3, live, now + 15);
    }
    CHECK(polls >= 10);
    CHECK_EQ(k.frames(), (uint32_t)polls);
    CHECK(live.fresh(kRpm, now) && live.value(kRpm) == 1520);
    CHECK(!strcmp(k.status(), "LIVE"));
    CHECK(!strncmp(k.lastDump(), "05 F0 1A", 8));

    // a corrupted frame is ignored, an adapter without echo works too
    Session n;
    n.restart(0);
    n.poll(0);
    n.wakeDone(200);
    n.poll(200);
    n.poll(400);  // hello sent
    const uint8_t noEcho[] = {0x02, 0x04, 0x00, 0xFA};
    n.feed(noEcho, 4, s, 420);
    CHECK(n.phase() == Session::Phase::Poll);
    a = n.poll(500);
    CHECK(a.kind == Session::Action::Send);
    uint8_t bad[sizeof(frame)];
    memcpy(bad, frame, sizeof(frame));
    bad[6] ^= 0xFF;
    Snapshot none;
    n.feed(bad, sizeof(bad), none, 510);
    CHECK(!none.fresh(kRpm, 510));
    // no answer at all: after a few misses it wakes the ECU up again
    uint32_t t = 500;
    for (int i = 0; i < 40; i++) {
        t += 100;
        n.poll(t);
    }
    CHECK(n.phase() == Session::Phase::Wake || n.phase() == Session::Phase::Hello);
}
