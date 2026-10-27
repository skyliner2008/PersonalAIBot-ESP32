/**
 * @file test_obd.cpp
 * @brief Host tests (T112): OBD-II PIDs / trouble codes / VIN / multi-frame replies, the ELM327 scan (protocols,
 *        supported PIDs, codes, freeze frame, clear, polling by focus, ignition off, baud), vehicle health + trip,
 *        the simulator's diagnosis
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "test_framework.h"
#include "vehicle/Elm327.h"
#include "vehicle/Obd.h"
#include "vehicle/ObdLog.h"
#include "vehicle/SimEngine.h"
#include "vehicle/VehicleData.h"
#include "vehicle/VehicleHealth.h"

using namespace vehicle;

TEST(vehicle_obd_pids_and_codes) {
    using namespace vehicle::obd;
    float v = 0;
    const uint8_t rpm[] = {0x1A, 0xF8};
    CHECK(decodePid(0x0C, rpm, 2, v) && fabsf(v - 1726) < 0.1f);
    CHECK(!decodePid(0x0C, rpm, 1, v));  // two bytes needed
    const uint8_t a80[] = {0x80}, a85[] = {0x85}, a40[] = {0x40}, a7b[] = {0x7B};
    CHECK(decodePid(0x04, a80, 1, v) && fabsf(v - 50.2f) < 0.1f);  // load %
    CHECK(decodePid(0x06, a85, 1, v) && fabsf(v - 3.9f) < 0.1f);   // fuel trim: (133 - 128) * 100 / 128
    CHECK(decodePid(0x07, a40, 1, v) && fabsf(v + 50.0f) < 0.1f);  // -50 %
    CHECK(decodePid(0x0E, a80, 1, v) && fabsf(v) < 0.1f);          // timing 128 / 2 - 64
    CHECK(decodePid(0x5C, a7b, 1, v) && v == 83);                    // oil -40
    const uint8_t maf[] = {0x03, 0xE8}, volt[] = {0x3A, 0x98}, rate[] = {0x00, 0x64}, km[] = {0x01, 0x20};
    CHECK(decodePid(0x10, maf, 2, v) && fabsf(v - 10.0f) < 0.01f);   // 1000 / 100 g/s
    CHECK(decodePid(0x42, volt, 2, v) && fabsf(v - 15.0f) < 0.01f);  // 15000 mV
    CHECK(decodePid(0x5E, rate, 2, v) && fabsf(v - 5.0f) < 0.01f);   // 100 / 20 l/h
    CHECK(decodePid(0x31, km, 2, v) && v == 288);
    CHECK(!decodePid(0x99, a80, 1, v));
    size_t n = 0;
    const Pid* t = pids(n);
    CHECK(n <= Elm327::kTable && t[0].pid == 0x0C);  // rpm first: Elm327 gives it its own slots
    for (size_t i = 0; i < n; i++) {
        if (t[i].ch < kChannelCount) CHECK(t[i].bytes >= 1 && t[i].gaugeMs <= t[i].backMs);
    }
    CHECK(findPid(0x2F) && findPid(0x2F)->ch == kFuelLevel);

    const uint8_t mon[] = {0x81, 0x07, 0x61, 0xA0};  // light on, 1 code; 3 continuous + 3 others, 1 not complete
    const Readiness r = decodeMonitor(mon);
    CHECK(r.valid && r.mil && r.count == 1 && r.total == 6 && r.done == 5 && !r.compression);

    // codes: two bytes <-> text
    char code[6];
    formatDtc(0x0171, code);
    CHECK(!strcmp(code, "P0171"));
    formatDtc(0xC100, code);
    CHECK(!strcmp(code, "U0100"));
    formatDtc(0x5234, code);
    CHECK(!strcmp(code, "C1234"));
    uint16_t raw = 0;
    CHECK(parseDtc(" p0420", raw) && raw == 0x0420);
    CHECK(parseDtc("B0001,", raw) && raw == 0x8001);
    CHECK(!parseDtc("P4171", raw) && !parseDtc("X0171", raw) && !parseDtc("P017", raw) && !parseDtc("P01711", raw));
    CHECK(!parseDtc(nullptr, raw) && !parseDtc("", raw));

    DtcInfo d = describeDtc(0x0171);
    CHECK(strstr(d.text, "lean") && strstr(d.text, "bank 1") && d.generic && d.severity == 2);
    d = describeDtc(0x0302);
    CHECK(!strcmp(d.text, "Cylinder 2 misfire detected") && d.severity == 3);
    d = describeDtc(0x0137);
    CHECK(!strcmp(d.text, "O2 sensor circuit low voltage (bank 1 sensor 2)"));
    d = describeDtc(0x0161);
    CHECK(!strcmp(d.text, "O2 sensor heater circuit (bank 2 sensor 2)"));
    d = describeDtc(0x0353);
    CHECK(strstr(d.text, "Ignition coil C") && strstr(d.text, "cylinder 3"));
    d = describeDtc(0x0420);
    CHECK(strstr(d.text, "Catalyst") && d.severity == 2);
    d = describeDtc(0xC100);
    CHECK(strstr(d.text, "engine control module") && d.severity == 3);
    d = describeDtc(0x1456);  // P1456: the carmaker's own
    CHECK(!d.generic && !d.text[0] && strstr(d.system, "emission"));
    d = describeDtc(0x5234);  // C1234
    CHECK(!d.generic && strstr(d.system, "chassis"));
    d = describeDtc(0x0699);  // generic, not in the table: its system at least
    CHECK(d.generic && !d.text[0] && strstr(d.system, "computer"));
    CHECK(strlen(describeDtc(0x0300).systemTh) > 0);

    DtcList list;
    list.add(0x0171);
    list.add(0x0171);  // two ECUs, one code
    list.add(0);       // padding
    CHECK(list.n == 1 && list.has(0x0171));
}

TEST(vehicle_obd_replies) {
    using namespace vehicle::obd;
    Msg m[6];
    // CAN multi-frame VIN: the length line, then numbered frames (the last one padded)
    size_t n = messages("014\n0:490201314434\n1:47503030523535\n2:42313233343536\n", m, 6);
    CHECK_EQ(n, (size_t)1);
    CHECK_EQ((int)m[0].n, 20);
    char vin[18];
    CHECK(parseVin(m, n, vin) && !strcmp(vin, "1D4GP00R55B123456"));
    // older buses: five numbered lines, the first padded with 00
    n = messages("SEARCHING...\n49020100000031\n49020244344750\n49020330305235\n49020435423132\n49020533343536\n", m, 6);
    CHECK_EQ(n, (size_t)5);
    CHECK(parseVin(m, n, vin) && !strcmp(vin, "1D4GP00R55B123456"));
    n = messages("49020100000031\n49020244344750\n", m, 6);
    CHECK(!parseVin(m, n, vin));  // lines missing
    CHECK_EQ(messages("NO DATA\n", m, 6), (size_t)0);

    // codes: CAN (count byte), two ECUs answering, and a multi-frame list
    DtcList codes;
    n = messages("430201710300\n4301C100\n", m, 6);
    CHECK_EQ(parseDtcReply(m, n, 0x43, 6, codes), 2);
    CHECK(codes.n == 3 && codes.has(0x0171) && codes.has(0x0300) && codes.has(0xC100));
    codes.clear();
    n = messages("00A\n0:430401710300\n1:04200455000000\n", m, 6);
    CHECK_EQ(parseDtcReply(m, n, 0x43, 6, codes), 1);
    CHECK(codes.n == 4 && codes.has(0x0420) && codes.has(0x0455));
    codes.clear();
    n = messages("43017101330000\n", m, 6);  // ISO 9141 / KWP: three per line, 0000 = none
    CHECK_EQ(parseDtcReply(m, n, 0x43, 3, codes), 1);
    CHECK(codes.n == 2 && codes.has(0x0171) && codes.has(0x0133));
    codes.clear();
    CHECK_EQ(parseDtcReply(m, n, 0x43, 0, codes), 1);  // protocol unknown: 7 bytes = the older format
    CHECK(codes.n == 2);
    codes.clear();
    n = messages("430201710300\n", m, 6);
    parseDtcReply(m, n, 0x43, 0, codes);  // protocol unknown: the length fits a count byte
    CHECK(codes.n == 2 && codes.has(0x0300));
    codes.clear();
    n = messages("4700\n", m, 6);
    CHECK_EQ(parseDtcReply(m, n, 0x47, 6, codes), 1);  // answered: no pending codes
    CHECK(codes.n == 0);
    CHECK_EQ(parseDtcReply(m, n, 0x43, 6, codes), 0);  // not a mode 03 reply

    CHECK_EQ(parseProtocol("A6\n"), 6);
    CHECK_EQ(parseProtocol("3"), 3);
    CHECK_EQ(parseProtocol("AC"), 12);
    CHECK_EQ(parseProtocol("OK"), 0);
    CHECK_EQ(parseProtocol("?"), 0);
    CHECK(isCan(6) && isCan(9) && !isCan(5) && !isCan(0));
    CHECK(strstr(protocolName(6), "CAN 11 bit 500") && strstr(protocolName(3), "9141"));
}

namespace {
// a scripted car behind an ELM327 v2.1: CAN 11 bit 500k, codes P0420 (stored) + P0171 (pending), a VIN
struct FakeCar {
    bool echo = true, on = true, codes = true, refuseClear = false, asleep = false;
    std::string proto = "A6";
    std::set<std::string> silent;  // PIDs that answer NO DATA although the car says it has them
    std::set<std::string> hang;    // commands the adapter never finishes (no prompt)
    std::string rpm = "410C1AF8";  // 1726 rpm
    std::string reply(const std::string& cmd) {
        if (hang.count(cmd)) return "";
        if (cmd == "010C" && on && !asleep) return std::string(echo ? cmd + "\r" : "") + rpm + "\r\r>";
        std::string r = echo ? cmd + "\r" : "";
        auto ok = [&](const std::string& body) { return r + body + "\r\r>"; };
        static const std::map<std::string, std::string> kLive = {
            {"0100", "SEARCHING...\r41009E3A8001"},  // 01 04 05 06 07 0B 0C 0D 0F 11 + next range
            {"0120", "41200002A001"},                // 2F 31 33 + next range
            {"0140", "414044008000"},                // 42 46 51
            {"0151", "415101"},                      // petrol
            {"010C", "410C1AF8"},   {"010D", "410D3C"},   {"0105", "41057B"},   {"010F", "410F48"},
            {"0111", "411140"},     {"010B", "410B21"},   {"0104", "410480"},   {"0106", "410685"},
            {"0107", "41078F"},     {"012F", "412F80"},   {"0131", "41310120"}, {"0133", "413365"},
            {"0142", "41423A98"},   {"0146", "414648"},   {"020C00", "420C002260"}, {"020D00", "420D0048"},
            {"020500", "4205007B"}, {"020400", "42040064"}, {"020600", "42060085"}, {"020700", "4207008F"},
            {"020B00", "420B0021"}, {"021100", "42110040"},
        };
        if (cmd == "ATZ") return ok("\rELM327 v2.1");
        if (cmd == "ATE0") {
            echo = false;
            return ok("OK");
        }
        if (cmd == "ATRV") return ok("12.6V");
        if (cmd == "ATDPN") return ok(proto);
        if (cmd.rfind("AT", 0) == 0) return ok("OK");
        if (!on) return ok("SEARCHING...\rUNABLE TO CONNECT");
        if (asleep || silent.count(cmd)) return ok("NO DATA");
        if (cmd == "03") return ok(codes ? "43010420" : "4300");
        if (cmd == "07") return ok(codes ? "47010171" : "4700");
        if (cmd == "0A") return ok("NO DATA");
        if (cmd == "020200") return ok(codes ? "4202000420" : "4202000000");
        if (cmd == "0101") return ok(codes ? "4101810761A0" : "410100076161");
        if (cmd == "0902") return ok("014\r0:490201314434\r1:47503030523535\r2:42313233343536");
        if (cmd == "04") {
            if (refuseClear) return ok("7F0422");
            codes = false;
            return ok("44");
        }
        const auto it = kLive.find(cmd);
        return ok(it != kLive.end() ? it->second : "NO DATA");
    }
};

void drive(Elm327& elm, FakeCar& car, Snapshot& s, uint32_t& now, int steps, std::vector<std::string>* log = nullptr,
           bool answer = true) {
    for (int i = 0; i < steps; i++) {
        now += 10;
        if (const char* cmd = elm.poll(now)) {
            if (log) log->push_back(cmd);
            if (!answer) continue;
            const std::string r = car.reply(cmd);
            elm.feed(r.data(), r.size(), s, now);
        }
    }
}

bool sent(const std::vector<std::string>& log, const char* cmd) {
    for (const std::string& c : log) {
        if (c == cmd) return true;
    }
    return false;
}

bool has(const Health& h, Finding f, uint8_t sev) {
    for (int i = 0; i < h.n; i++) {
        if (h.issue[i].id == f && h.issue[i].sev == sev) return true;
    }
    return false;
}
}  // namespace

TEST(vehicle_elm327_scan) {
    FakeCar car;
    Elm327 elm;
    Snapshot s;
    uint32_t now = 5000;
    elm.setProtocolHint(6);  // CAN worked last time: tried first
    elm.restart(now);
    std::vector<std::string> log;
    drive(elm, car, s, now, 2000, &log);  // 20 s
    CHECK(sent(log, "ATSPA6") && !sent(log, "ATSP0"));
    CHECK(elm.phase() == Elm327::Phase::Poll);
    const obd::Diag& d = elm.diag();
    CHECK(d.available && d.protocol == 6);
    CHECK(elm.supports(0x2F) && elm.supports(0x42) && elm.supports(0x51) && !elm.supports(0x5E));
    CHECK_EQ((int)d.supportedPids, 16);  // 10 + 3 + 3, the "next range" bits not counted
    CHECK(!strcmp(d.vin, "1D4GP00R55B123456"));
    CHECK(d.fuelType == 1);
    CHECK(d.codesValid && d.scanSeq >= 1);
    CHECK(d.stored.n == 1 && d.stored.has(0x0420) && d.pending.n == 1 && d.pending.has(0x0171) && d.permanent.n == 0);
    CHECK(d.ready.valid && d.ready.mil && d.ready.count == 1 && d.ready.done == 5 && d.ready.total == 6);
    CHECK(d.freeze.dtc == 0x0420 && d.freeze.n == 8);
    bool freezeRpm = false;
    for (int i = 0; i < d.freeze.n; i++) freezeRpm |= d.freeze.pid[i] == 0x0C && fabsf(d.freeze.value[i] - 2200) < 0.1f;
    CHECK(freezeRpm);
    // the extra PIDs, a slow one with a long freshness
    CHECK(s.fresh(kLoad, now) && s.fresh(kStftB1, now) && s.fresh(kLtftB1, now) && s.fresh(kFuelLevel, now));
    CHECK(fabsf(s.value(kLtftB1) - 11.7f) < 0.1f && fabsf(s.value(kFuelLevel) - 50.2f) < 0.1f);
    CHECK(s.fresh(kBaro, now) && s.fresh(kDistClear, now) && s.value(kDistClear) == 288);
    CHECK((uint32_t)s.ch[kFuelLevel].ttl * 10 >= 60000u);  // asked every 20 s: fresh for a minute
    CHECK(!sent(log, "0142"));  // the adapter measures the battery (ATRV): module voltage not needed

    // a PID the car claims but never answers: dropped after three NO DATA
    car.silent.insert("010F");
    drive(elm, car, s, now, 1500);
    CHECK(elm.dropped(0x0F) && !elm.dropped(0x0D));
    CHECK(elm.phase() == Elm327::Phase::Poll);  // the others still answer: not "ignition off"

    // asked to scan again: the codes are read now, not in 5 minutes
    const uint32_t seq = elm.diag().scanSeq;
    log.clear();
    elm.requestScan();
    drive(elm, car, s, now, 300, &log);
    CHECK(sent(log, "03") && sent(log, "07") && elm.diag().scanSeq == seq + 1);

    // clearing: 04, then read again - no codes, the light off, the monitors start over
    car.refuseClear = true;
    elm.requestClear();
    drive(elm, car, s, now, 500);
    CHECK(elm.diag().clearResult == obd::kClearRefused && elm.diag().stored.n == 1);
    car.refuseClear = false;
    log.clear();
    elm.requestClear();
    drive(elm, car, s, now, 500, &log);
    CHECK(sent(log, "04"));
    CHECK(elm.diag().clearResult == obd::kClearOk);
    CHECK(elm.diag().stored.n == 0 && elm.diag().pending.n == 0 && !elm.diag().ready.mil && elm.diag().ready.done == 3);
    CHECK(elm.diag().freeze.dtc == 0);

    // dash closed: only what is due - far fewer requests than with a gauge on screen
    auto rate = [&](Elm327::Focus f) {
        elm.setFocus(f);
        std::vector<std::string> l;
        drive(elm, car, s, now, 1000, &l);
        return l.size();
    };
    const size_t gauge = rate(Elm327::Focus::Gauge), background = rate(Elm327::Focus::Background);
    CHECK(background * 5 < gauge);
    CHECK(s.fresh(kRpm, now) && s.fresh(kSpeed, now) && s.fresh(kFuelLevel, now));  // still fresh, slower
    printf("    elm327: %u requests / 10 s with a gauge, %u with the dash closed\n", (unsigned)gauge, (unsigned)background);

    // the engine stops and the ECU sleeps: NO DATA everywhere -> NO ECU, the parked battery still reads
    car.asleep = true;
    drive(elm, car, s, now, 1500);
    CHECK(elm.phase() == Elm327::Phase::NoEcu || elm.phase() == Elm327::Phase::Search);
    CHECK(!strcmp(elm.status(), "NO ECU") || !strcmp(elm.status(), "ECU SEARCH"));
    drive(elm, car, s, now, 1500);
    CHECK(s.fresh(kBattery, now) && (uint32_t)s.ch[kBattery].ttl * 10 >= 20000u);
    car.asleep = false;  // back on: found again, the dropped PID asked again
    car.silent.clear();
    drive(elm, car, s, now, 1000);
    CHECK(elm.phase() == Elm327::Phase::Poll && !elm.dropped(0x0F));
}

TEST(vehicle_elm327_protocols_and_baud) {
    // the automatic search fails twice: each protocol in turn, CAN first
    FakeCar off;
    off.on = false;
    Elm327 elm;
    Snapshot s;
    uint32_t now = 0;
    elm.restart(now);
    std::vector<std::string> log;
    drive(elm, off, s, now, 6000, &log);  // 60 s
    size_t at6 = 0, at8 = 0;
    for (size_t i = 0; i < log.size(); i++) {
        if (log[i] == "ATSP6" && !at6) at6 = i;
        if (log[i] == "ATSP8" && !at8) at8 = i;
    }
    CHECK(at6 && at8 && at6 < at8);
    CHECK(elm.phase() != Elm327::Phase::Poll);
    CHECK(s.fresh(kBattery, now));  // ATRV while there is no ECU: the battery of a parked car

    // an old ECU that never answers the VIN / permanent codes: those steps are skipped, the adapter is not reset
    FakeCar old;
    old.hang = {"0902", "0A"};
    Elm327 e3;
    uint32_t t3 = 0;
    e3.restart(t3);
    std::vector<std::string> l3;
    drive(e3, old, s, t3, 3000, &l3);
    int resets = 0;
    for (const std::string& c : l3) resets += c == "ATZ";
    CHECK_EQ(resets, 1);
    CHECK(e3.phase() == Elm327::Phase::Poll && e3.diag().codesValid && e3.diag().stored.has(0x0420) && !e3.diag().vin[0]);

    // an adapter that never answers (wrong baud rate): counted, so the caller can try another speed
    FakeCar mute;
    Elm327 e2;
    uint32_t t = 0;
    e2.restart(t);
    drive(e2, mute, s, t, 1000, nullptr, false);
    CHECK(e2.silentResets() >= 1 && !e2.answered());
    CHECK(!strcmp(e2.status(), "NO ADAPTER") || !strcmp(e2.status(), "ELM RESET"));
}

TEST(vehicle_health) {
    HealthConfig cfg;  // a 1.5 l petrol car
    // ---- fuel flow from what the car has ----
    Snapshot s;
    uint8_t from = 0;
    s.set(kRpm, 2500, 0);
    s.set(kMaf, 10.0f, 0);
    CHECK(fabsf(HealthMonitor::fuelFlow(s, 0, cfg, from) - 3.29f) < 0.01f && from == 2);
    Snapshot m;
    m.set(kRpm, 2500, 0);
    m.set(kMap, 46, 0);
    m.set(kIntake, 30, 0);
    const float est = HealthMonitor::fuelFlow(m, 0, cfg, from);
    CHECK(from == 3 && est > 3.5f && est < 5.5f);
    HealthConfig diesel = cfg;
    diesel.diesel = true;
    CHECK(HealthMonitor::fuelFlow(s, 0, diesel, from) == 0 && from == 0);
    s.set(kFuelRate, 2.5f, 0);
    CHECK(HealthMonitor::fuelFlow(s, 0, diesel, from) == 2.5f && from == 1);
    CHECK(HealthMonitor::restPercent(12.7f) == 100 && HealthMonitor::restPercent(12.2f) == 40);
    CHECK(HealthMonitor::restPercent(11.7f) == 0 && HealthMonitor::restPercent(12.45f) == 75);

    // ---- a drive: 60 km/h on the MAF; lean trims ----
    {
        HealthMonitor hm;
        hm.reset(cfg);
        uint32_t now = 1000;
        Snapshot r;
        for (int i = 0; i < 4 * 150; i++, now += 250) {  // 150 s at 4 Hz
            r.set(kRpm, 2000, now);
            r.set(kSpeed, 60, now);
            r.set(kCoolant, 90, now);
            r.set(kBattery, 14.1f, now);
            r.set(kMaf, 5.0f, now);
            r.set(kStftB1, 8, now);
            r.set(kLtftB1, 15, now);  // +23 % in total
            hm.update(r, nullptr, now);
        }
        const Health& h = hm.health();
        CHECK(h.running && h.trimValid[0] && fabsf(h.trim[0] - 23) < 0.5f && !h.trimValid[1]);
        CHECK(has(h, Finding::LeanB1, kSevWarn));
        CHECK(h.worst == kSevWarn);
        CHECK(fabsf(h.trip.km - 2.5f) < 0.05f);  // 150 s at 60 km/h
        CHECK(h.trip.fuelFrom == 2 && fabsf(h.trip.lph - 1.64f) < 0.01f);
        CHECK(fabsf(h.trip.avgKmL() - 36.5f) < 1.0f && fabsf(h.trip.instKmL - 36.5f) < 1.0f);
        CHECK(fabsf(h.trip.runMin - 2.5f) < 0.05f && h.trip.maxKmh == 60);
        printf("    health: %.2f km, %.3f l, %.1f km/l, trim B1 %+.1f %%\n", h.trip.km, h.trip.fuelL, h.trip.avgKmL(),
               h.trim[0]);
        hm.resetTrip();
        CHECK(hm.health().trip.km == 0);
    }
    // ---- lean only at idle: an air leak ----
    {
        HealthMonitor hm;
        hm.reset(cfg);
        uint32_t now = 1000;
        Snapshot r;
        for (int i = 0; i < 4 * 240; i++, now += 250) {
            const bool idle = (i / 80) % 2 == 0;  // 20 s idle, 20 s cruise
            r.set(kRpm, idle ? 800 : 2200, now);
            r.set(kSpeed, idle ? 0 : 70, now);
            r.set(kCoolant, 88, now);
            r.set(kStftB1, idle ? 12 : 1, now);
            r.set(kLtftB1, idle ? 8 : 2, now);
            hm.update(r, nullptr, now);
        }
        CHECK(has(hm.health(), Finding::LeanAtIdle, kSevInfo));
    }
    // ---- not charging; overheating that climbs; then the red ----
    {
        HealthMonitor hm;
        hm.reset(cfg);
        uint32_t now = 1000;
        Snapshot r;
        float coolant = 92;
        bool rising = false, notCharging = false;
        for (int i = 0; i < 4 * 150; i++, now += 250) {
            coolant += 4.0f / 240.0f;  // +4 C a minute: 102 after 150 s, under the 105 warning
            r.set(kRpm, 1500, now);
            r.set(kSpeed, 20, now);
            r.set(kCoolant, coolant, now);
            r.set(kBattery, 12.0f, now);
            hm.update(r, nullptr, now);
            rising |= has(hm.health(), Finding::OverheatRising, kSevWarn);
            notCharging |= has(hm.health(), Finding::NotCharging, kSevCrit);
        }
        CHECK(rising && notCharging);
        CHECK(hm.health().coolantRateValid && fabsf(hm.health().coolantRate - 4.0f) < 0.5f);
        for (int i = 0; i < 20; i++, now += 250) {
            r.set(kRpm, 1500, now);
            r.set(kCoolant, 114, now);
            r.set(kBattery, 12.0f, now);
            hm.update(r, nullptr, now);
        }
        CHECK(has(hm.health(), Finding::Overheat, kSevCrit));
        CHECK(hm.health().issue[0].sev == kSevCrit);  // most serious first
    }
    // ---- a weak battery: low parked, a deep dip while cranking ----
    {
        HealthMonitor hm;
        hm.reset(cfg);
        uint32_t now = 1000;
        Snapshot r;
        for (int i = 0; i < 4 * 100; i++, now += 250) {  // parked 100 s at 12.1 V (ignition on, rpm 0)
            r.set(kRpm, 0, now);
            r.set(kBattery, 12.1f, now);
            hm.update(r, nullptr, now);
        }
        CHECK(hm.health().restValid && hm.health().restPct == 25);
        CHECK(has(hm.health(), Finding::LowBatteryParked, kSevWarn));
        r.set(kRpm, 200, now);  // cranking
        r.set(kBattery, 9.3f, now);
        hm.update(r, nullptr, now);
        now += 250;
        r.set(kRpm, 850, now);  // it caught
        r.set(kBattery, 13.9f, now);
        hm.update(r, nullptr, now);
        CHECK(fabsf(hm.health().startMinV - 9.3f) < 0.01f);
        CHECK(has(hm.health(), Finding::WeakStart, kSevWarn));
    }
    // ---- rough idle, low fuel, trouble codes ----
    {
        HealthMonitor hm;
        hm.reset(cfg);
        uint32_t now = 1000;
        Snapshot r;
        obd::Diag d;
        d.available = true;
        d.ready.valid = d.ready.mil = true;
        d.ready.count = 2;
        d.stored.add(0x0300);  // misfire: serious
        d.pending.add(0x0171);
        for (int i = 0; i < 4 * 60; i++, now += 250) {
            r.set(kRpm, (i & 1) ? 1000 : 600, now);
            r.set(kSpeed, 0, now);
            r.set(kCoolant, 90, now);
            r.set(kFuelLevel, 8, now);
            hm.update(r, &d, now);
        }
        const Health& h = hm.health();
        CHECK(has(h, Finding::RoughIdle, kSevWarn));
        CHECK(has(h, Finding::LowFuel, kSevWarn));
        CHECK(has(h, Finding::StoredCodes, kSevCrit) && has(h, Finding::MilOn, kSevWarn) &&
              has(h, Finding::PendingCodes, kSevInfo));
        CHECK(h.worst == kSevCrit && h.issue[0].sev == kSevCrit);
        for (int i = 1; i < h.n; i++) CHECK(h.issue[i].sev <= h.issue[i - 1].sev);
    }
    // ---- a healthy drive says nothing ----
    {
        HealthMonitor hm;
        hm.reset(cfg);
        uint32_t now = 1000;
        Snapshot r;
        for (int i = 0; i < 4 * 120; i++, now += 250) {
            r.set(kRpm, 2100 + 50 * sinf(i * 0.3f), now);
            r.set(kSpeed, 70, now);
            r.set(kCoolant, 91, now);
            r.set(kBattery, 14.2f, now);
            r.set(kStftB1, 1.5f, now);
            r.set(kLtftB1, -2.0f, now);
            r.set(kFuelLevel, 60, now);
            hm.update(r, nullptr, now);
        }
        CHECK(hm.health().n == 0 && hm.health().worst == kSevOk);
    }
    for (int f = 0; f < (int)Finding::Count; f++) {
        CHECK(strlen(findingKey((Finding)f)) > 3 && strlen(findingTh((Finding)f)) > 3);
    }
}

TEST(vehicle_simulator_diag) {
    SimEngine car;
    car.reset(Kind::Car);
    for (int i = 0; i < 50 * 30; i++) car.step(0.02f);
    Snapshot s;
    car.write(s, 100);
    CHECK(s.fresh(kMaf, 100) && s.fresh(kLtftB1, 100) && s.fresh(kLoad, 100) && s.fresh(kFuelLevel, 100));
    CHECK(s.value(kMaf) > 1 && s.value(kMaf) < 80 && s.value(kFuelLevel) < 62 && s.value(kFuelLevel) > 55);
    obd::Diag d;
    car.diag(d);
    CHECK(d.available && d.simulated && d.codesValid && d.ready.mil && d.stored.has(0x0420) && d.pending.has(0x0171));
    CHECK(d.freeze.dtc == 0x0420 && d.freeze.n == 6);
    car.clearCodes();
    car.diag(d);
    CHECK(d.available && !d.ready.mil && d.stored.n == 0 && d.pending.n == 0 && d.ready.done == 2);
    SimEngine bike;
    bike.reset(Kind::Motorcycle);
    bike.step(0.02f);
    Snapshot b;
    bike.write(b, 5);
    CHECK(!b.fresh(kMaf, 5) && b.fresh(kMap, 5));  // like a K-Line bike: fuel from MAP
    bike.diag(d);
    CHECK(!d.available);

    // the simulator through the health analysis: a trip with fuel, the lean trims it has
    HealthMonitor hm;
    HealthConfig cfg;
    hm.reset(cfg);
    SimEngine sim;
    sim.reset(Kind::Car);
    sim.setAutoDrive(true);
    obd::Diag sd;
    sim.diag(sd);
    Snapshot snap;
    uint32_t now = 0;
    for (int i = 0; i < 50 * 240; i++) {  // 4 minutes
        sim.step(0.02f);
        now += 20;
        sim.write(snap, now);
        hm.update(snap, &sd, now);
    }
    const Health& h = hm.health();
    CHECK(h.trip.km > 0.5f && h.trip.fuelL > 0.02f && h.trip.fuelFrom == 2);
    CHECK(h.trip.avgKmL() > 5 && h.trip.avgKmL() < 60);
    CHECK(has(h, Finding::LeanB1, kSevInfo) && has(h, Finding::StoredCodes, kSevWarn));
    printf("    sim 4 min: %.2f km, %.3f l (%.1f km/l), %u findings, worst %s\n", h.trip.km, h.trip.fuelL,
           h.trip.avgKmL(), h.n, severityKey(h.worst));

    // VehicleData: the new channels, the new style, text lines with the new keys
    CHECK(!strcmp(channelKey(kFuelLevel), "fuel_pct") && !strcmp(channelKey(kDistClear), "km_since_clear"));
    CHECK(channelDecimals(kBattery) == 1 && channelDecimals(kRpm) == 0);
    DashStyle st;
    CHECK(parseStyle("info", st) && st == DashStyle::Info && parseStyle("diag", st) && st == DashStyle::Info);
    Snapshot t;
    CHECK_EQ(parseTextLine("rpm=3000 fuel=40 load=55 maf=12.5", t, 9), 4);
    CHECK(t.value(kFuelLevel) == 40 && t.value(kLoad) == 55 && fabsf(t.value(kMaf) - 12.5f) < 0.01f);
}

// ============================================================ T115: the OBD log on the SD card, replay

namespace {
// what VehicleLog writes, into a string (times from the start of the log)
struct StringLog : Elm327::Trace {
    std::string text = "#OBDLOG 1 fw=test source=obd_ble start=2026-10-27T15:04:05+07:00\n";
    uint32_t t0 = 0;
    void add(uint32_t now, char tag, const char* s) {
        char line[1200];
        const size_t n = vehicle::obdlog::formatRecord(line, sizeof(line), now - t0, tag, s);
        text.append(line, n);
    }
    void sent(uint32_t now, const char* cmd) override { add(now, '>', cmd); }
    void replied(uint32_t now, const char* reply) override { add(now, '<', reply); }
    void timedOut(uint32_t now, const char* cmd) override { add(now, '!', cmd); }
};

// the driver against a recording, on a simulated clock (fast-forward)
struct ReplayRun {
    Elm327 elm;
    vehicle::obdlog::ReplayAdapter ad;
    Snapshot s;
    uint32_t now = 100000;
    void begin(const vehicle::obdlog::Replay& r) {
        ad.begin(&r, now);
        elm.restart(now);
    }
    void step(int steps) {
        for (int i = 0; i < steps; i++) {
            now += 10;
            if (const char* cmd = elm.poll(now)) {
                ad.write(cmd, strlen(cmd), now);
                ad.write("\r", 1, now);
            }
            char buf[256];
            const size_t n = ad.read(buf, sizeof(buf), now);
            if (n) elm.feed(buf, n, s, now);
        }
    }
};
}  // namespace

TEST(vehicle_obdlog_format) {
    using namespace vehicle::obdlog;
    const char* tricky = "a|b\\c\td\r\ne\x01 \xE0\xB8\x81";  // pipe, backslash, tab, CR LF, a control byte, Thai
    char esc[128], back[128];
    escape(tricky, esc, sizeof(esc));
    CHECK(!strchr(esc, '\t') && !strchr(esc, '\n') && !strchr(esc, '\r') && strstr(esc, "\\x01") && strstr(esc, "\\|"));
    unescape(esc, strlen(esc), back, sizeof(back));
    CHECK(!strcmp(back, tricky));
    escape("SEARCHING...\n4100BE3EB811\n", esc, sizeof(esc), true);  // a reply: its line breaks become |
    CHECK(!strcmp(esc, "SEARCHING...|4100BE3EB811|"));
    unescape(esc, strlen(esc), back, sizeof(back), true);
    CHECK(!strcmp(back, "SEARCHING...\n4100BE3EB811\n"));
    char small[8];
    escape("\t\t\t\t", small, sizeof(small));  // never half an escape at the end
    CHECK(!strcmp(small, "\\t\\t\\t"));

    char line[200];
    const size_t n = formatRecord(line, sizeof(line), 12345, '<', "NO DATA\n");
    CHECK(!strcmp(line, "12345\t<\tNO DATA|\n") && n == strlen(line));
    Record r;
    CHECK(parseRecord(line, n, r) && r.t == 12345 && r.tag == '<' && r.len == 8 && !strncmp(r.text, "NO DATA|", 8));
    CHECK(parseRecord("7\tS\t", 4, r) && r.tag == 'S' && r.len == 0);
    CHECK(!parseRecord("#OBDLOG 1", 9, r) && !parseRecord("abc\tE\tx", 7, r) && !parseRecord("12", 2, r));

    Snapshot s, back2;
    s.set(kRpm, 812.4f, 10);
    s.set(kBattery, 14.12f, 10);
    s.set(kStftB1, -3.27f, 10);
    char vals[300];
    formatValues(s, 10, vals, sizeof(vals));
    CHECK(!strcmp(vals, "rpm=812 battery_v=14.12 stft_b1_pct=-3.3"));
    CHECK_EQ(parseValues(vals, strlen(vals), back2, 50, 3000), 3);
    CHECK(back2.value(kRpm) == 812 && fabsf(back2.value(kBattery) - 14.12f) < 0.001f && back2.fresh(kStftB1, 3000));
}

TEST(vehicle_obdlog_record_and_replay) {
    using namespace vehicle::obdlog;
    // record 20 s of the scripted car: the engine goes from 1726 to 3000 rpm at 10 s
    FakeCar car;
    Elm327 elm;
    StringLog log;
    Snapshot s;
    uint32_t now = 5000;
    log.t0 = now;
    elm.setTrace(&log);
    elm.restart(now);
    for (int sec = 0; sec < 20; sec++) {
        if (sec == 10) car.rpm = "410C2EE0";
        drive(elm, car, s, now, 100);
        char vals[400];
        formatValues(s, now, vals, sizeof(vals));
        log.add(now, 'D', vals);
        if (sec == 3) log.add(now, 'E', "Bluetooth on (dash opened)");
    }
    CHECK(log.text.size() > 5000);

    Replay rp;
    CHECK(rp.load(log.text.data(), log.text.size()));
    char src[24];
    CHECK(!strcmp(rp.header("source", src, sizeof(src)), "obd_ble"));
    CHECK(rp.hasAdapterTrace() && rp.hasValues() && rp.duration() >= 19000 && rp.duration() <= 20100);
    Replay::Answer a = rp.answer("010C", 5000);
    CHECK(a.known && !a.silent && !strcmp(a.text, "410C1AF8\n"));
    a = rp.answer("010C", 15000);
    CHECK(!strcmp(a.text, "410C2EE0\n"));
    a = rp.answer("ATZ", 0);
    CHECK(a.known && strstr(a.text, "ELM327 v2.1"));
    a = rp.answer("0902", 10000);
    CHECK(strstr(a.text, "0:490201314434\n1:"));
    a = rp.answer("01FF", 3000);
    CHECK(!a.known && !strcmp(a.text, "NO DATA"));
    a = rp.answer("ATSH7E0", 3000);
    CHECK(!a.known && !strcmp(a.text, "OK"));
    char bytes[600];
    const size_t nb = Replay::stream(rp.answer("010C", 5000), bytes, sizeof(bytes));
    CHECK(nb == 11 && !memcmp(bytes, "410C1AF8\r\r>", 11));
    Snapshot v;
    CHECK(rp.values(6000, v, 1) && fabsf(v.value(kRpm) - 1726) < 1 && v.fresh(kFuelLevel, 1));
    CHECK(rp.values(16000, v, 1) && fabsf(v.value(kRpm) - 3000) < 1);
    size_t cursor = 0;
    Record note;
    CHECK(rp.nextNote(0, 30000, cursor, note) && note.tag == 'E' && !strncmp(note.text, "Bluetooth on", 12));

    // the driver meets the recorded car again: same protocol, codes, VIN, and the rpm of each moment
    ReplayRun run;
    run.begin(rp);
    run.step(800);  // 8 s
    CHECK(run.elm.phase() == Elm327::Phase::Poll && run.elm.diag().protocol == 6);
    CHECK(run.elm.diag().stored.has(0x0420) && !strcmp(run.elm.diag().vin, "1D4GP00R55B123456"));
    CHECK(fabsf(run.s.value(kRpm) - 1726) < 1 && run.s.fresh(kCoolant, run.now));
    run.step(500);  // 13 s: after the change
    CHECK(fabsf(run.s.value(kRpm) - 3000) < 1);
    CHECK(run.ad.asked() > 100 && run.ad.unknown() == 0);
    printf("    replay: %u answers from %u log records, %.1f s of drive\n", (unsigned)run.ad.asked(),
           (unsigned)rp.records(), rp.duration() / 1000.0f);

    // a recorded timeout is replayed as silence; at the end the log starts over
    const char* tiny =
        "#OBDLOG 1 source=obd_uart\n0\t>\tATZ\n10\t<\tELM327 v1.5|\n20\t>\t010D\n1520\t!\t010D\n1530\t>\t010D\n"
        "1600\t<\t410D3C|\n5000\tS\tLIVE\n";
    Replay t2;
    CHECK(t2.load(tiny, strlen(tiny)) && t2.duration() == 5000);
    CHECK(t2.answer("010D", 100).silent && !t2.answer("010D", 1600).silent);
    ReplayAdapter ad;
    ad.begin(&t2, 0);
    CHECK(ad.logTime(2000) == 2000 && ad.logTime(6000) == 0 && ad.loops(6000) == 1);  // 5 s + a second's pause
    ad.write("010D\r", 5, 1700);
    char buf[64];
    CHECK(ad.read(buf, sizeof(buf), 1700) == 0);  // the recorded 70 ms latency first
    const size_t got = ad.read(buf, sizeof(buf), 1770);
    CHECK(got == 9 && !memcmp(buf, "410D3C\r\r>", 9));
    ad.write("010D\r", 5, 6100);  // the next round: at log time 100 the car did not answer
    CHECK(ad.read(buf, sizeof(buf), 9000) == 0 && ad.silent() == 1);
}

// ---- host_tests.exe --replay <file.log>: a drive recorded in the car, through this driver (wiki 16) ----
int obdReplayCli(const char* path) {
    using namespace vehicle::obdlog;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        printf("cannot open %s\n", path);
        return 2;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string data = ss.str();
    Replay rp;
    if (!rp.load(data.data(), data.size())) {
        printf("%s: no records\n", path);
        return 2;
    }
    char src[32], start[40];
    printf("%s: %u records, %.1f s, source %s, start %s, adapter trace %s\n", path, (unsigned)rp.records(),
           rp.duration() / 1000.0f, rp.header("source", src, sizeof(src)), rp.header("start", start, sizeof(start)),
           rp.hasAdapterTrace() ? "yes" : "no (values only)");
    ReplayRun run;
    run.begin(rp);
    uint32_t lastPrint = 0, diagVer = 0;
    size_t cursor = 0;
    uint32_t prevLog = 0;  // the notes before the first command too (the Bluetooth connect)
    {
        Record note;
        while (rp.nextNote(0, rp.start(), cursor, note)) {
            printf("  %7.1f  [%c] %.*s\n", note.t / 1000.0f, note.tag, (int)note.len, note.text);
        }
        prevLog = rp.start();
    }
    const char* lastStatus = "";
    while (run.ad.loops(run.now) == 0) {
        run.step(10);  // 100 ms
        const uint32_t lt = run.ad.logTime(run.now);
        Record note;
        while (rp.nextNote(prevLog, lt, cursor, note)) {
            printf("  %7.1f  [%c] %.*s\n", note.t / 1000.0f, note.tag, (int)note.len, note.text);
        }
        prevLog = lt;
        if (!rp.hasAdapterTrace()) rp.values(lt, run.s, run.now);
        if (strcmp(run.elm.status(), lastStatus)) {
            lastStatus = run.elm.status();
            if (rp.hasAdapterTrace()) printf("  %7.1f  driver: %s\n", lt / 1000.0f, lastStatus);
        }
        if (run.elm.diagVersion() != diagVer && rp.hasAdapterTrace()) {
            diagVer = run.elm.diagVersion();
            const obd::Diag& d = run.elm.diag();
            if (d.codesValid) {
                std::string codes;
                for (uint8_t i = 0; i < d.stored.n; i++) {
                    char c[6];
                    obd::formatDtc(d.stored.code[i], c);
                    codes += std::string(" ") + c;
                }
                for (uint8_t i = 0; i < d.pending.n; i++) {
                    char c[6];
                    obd::formatDtc(d.pending.code[i], c);
                    codes += std::string(" ") + c + "(pending)";
                }
                printf("  %7.1f  diag: proto %u, mil %d, ready %u/%u, %u PIDs, vin %s, codes:%s\n", lt / 1000.0f,
                       d.protocol, d.ready.mil, d.ready.done, d.ready.total, d.supportedPids, d.vin[0] ? d.vin : "-",
                       codes.empty() ? " none" : codes.c_str());
            }
        }
        if (lt - lastPrint >= 1000 || lt < lastPrint) {
            lastPrint = lt;
            char vals[500];
            formatValues(run.s, run.now, vals, sizeof(vals));
            printf("  %7.1f  %s\n", lt / 1000.0f, vals[0] ? vals : "(no fresh values)");
        }
    }
    printf("asked %u commands: %u not in the log, %u answered with silence (timed out when recorded)\n",
           (unsigned)run.ad.asked(), (unsigned)run.ad.unknown(), (unsigned)run.ad.silent());
    return 0;
}
