/**
 * @file VehicleLog.h
 * @brief The OBD log on the SD card (T115): everything the vehicle sources do, for reading and replaying later
 * @see vehicle/ObdLog.h (the format), wiki/16-Vehicle-Dash.md
 *
 * <agentDir>/obdlog/20261027-150405-obd_ble.log - one file per run of a real source (OBD over Bluetooth: per dash
 * opening; wired: from the start, closed after 10 min without an ECU), a new file every 2 MB, the oldest deleted past
 * 200 files / 256 MB. Records go to a PSRAM buffer (any task, never blocks); a low-priority task writes them and
 * syncs every 2 s, so pulling the power when the car is switched off loses at most the last two seconds.
 * The SD card only: on the internal flash (LittleFS) the 2.6 MB would wear out and fill up - off there.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>

#include "Elm327.h"

class VehicleLog : public vehicle::Elm327::Trace {
public:
    static constexpr size_t kMaxFileBytes = 2u * 1024 * 1024;
    static constexpr size_t kMaxFiles = 200;
    static constexpr uint64_t kMaxTotalBytes = 256ull * 1024 * 1024;

    /** At boot: logs go to <agentDir>/obdlog, only when agentDir is on the SD card. */
    void begin(const char* agentDir);
    bool available() const { return _sd; }
    bool enabled() const { return _enabled; }
    void setEnabled(bool on);

    /** A new file for a run of `source` (blocks ~50 ms for the file). Nothing when off / no SD card. */
    void start(const char* source);
    /** Flush and close (blocks until written). */
    void stop();
    bool active() const { return _active; }

    /** A record now (any task): the tags of ObdLog.h. */
    void record(char tag, const char* text) { add(millis(), tag, text); }
    void recordf(char tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    // Elm327::Trace - the adapter traffic
    void sent(uint32_t now, const char* cmd) override { add(now, '>', cmd); }
    void replied(uint32_t now, const char* reply) override { add(now, '<', reply); }
    void timedOut(uint32_t now, const char* cmd) override { add(now, '!', cmd); }

    /** Status for the console / menu / tool. */
    void describe(JsonObject out);
    /** Log files, newest first (name + bytes), at most `cap`. */
    struct FileInfo {
        char name[48];
        uint32_t bytes;
    };
    size_t list(FileInfo* out, size_t cap);
    /** Full path of a log file of the folder ("latest" = the newest). False if the name is not a plain one there. */
    bool path(const char* name, char* out, size_t cap);
    /** Delete one ("all" = every file but the one being written). Returns how many. */
    int remove(const char* name);

private:
    enum Cmd : uint8_t { kCmdNone, kCmdOpen, kCmdClose };
    char _dir[72] = "";
    bool _sd = false;
    volatile bool _enabled = true, _active = false;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    char* _buf[2] = {nullptr, nullptr};  // PSRAM: one fills while the other is written
    size_t _len = 0;
    uint8_t _cur = 0;
    volatile uint32_t _dropped = 0;
    uint32_t _t0 = 0;
    char _source[16] = "";
    char _name[48] = "";
    FILE* _f = nullptr;
    volatile uint32_t _fileBytes = 0;
    uint32_t _lastSync = 0;
    TaskHandle_t _task = nullptr;
    volatile uint8_t _cmd = kCmdNone;

    void add(uint32_t now, char tag, const char* text);
    static void taskEntry(void* self);
    void loop();
    void writeOut();
    bool openFile();
    void closeFile();
    void cleanup();
    void request(Cmd c);
};

extern VehicleLog gVehicleLog;
