/**
 * @file Vehicle.h
 * @brief Vehicle dash back end: the active data source in its own task, its settings, and the shared readings
 * @see wiki/16-Vehicle-Dash.md
 *
 * Sources (vehicle::SourceId): built-in simulator (driving by itself / finger = throttle), Honda K-Line,
 * OBD-II ELM327 over Bluetooth LE or a UART wire, text lines from another board. The UART ones use the
 * UART header (GPIO 43 TX, 44 RX), so not together with a device on those pins (agent/devices.json).
 *
 * Bluetooth: Arduino frees the BLE controller memory (~36 KB internal RAM) at boot unless something says
 * BLE is in use. bleInUse() (here) says so only when the saved source is OBD over BLE - picking that
 * source therefore takes one restart, and everything else keeps the RAM it had.
 *
 * Settings live in NVS "veh"; agent/vehicle.json may override the Honda K-Line table layout.
 * The task exists only once a source other than "off" was chosen (no cost for a board without a car).
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "DashModel.h"
#include "HondaKline.h"
#include "VehicleData.h"

class VehicleLink;

class Vehicle {
public:
    struct Settings {
        vehicle::SourceId source = vehicle::SourceId::Off;
        vehicle::Kind kind = vehicle::Kind::Car;
        uint16_t shiftRpm = 0;  // 0 = the kind's default
        uint16_t rpmMax = 0;
        char bleAddr[18] = "";  // ELM327 BLE adapter found by the last scan ("" = scan)
        uint8_t bleType = 0;
        uint32_t elmBaud = 38400;   // wired ELM327 / STN (ObdUart)
        uint32_t textBaud = 115200; // SerialText
        vehicle::DashStyle style = vehicle::DashStyle::Race;
    };

    /** Load the settings (+ agent/vehicle.json) and start the saved source. Call once at boot. */
    void begin(const char* agentDir);

    Settings settings();
    vehicle::DashConfig dashConfig();
    /** nullptr = ok, else why not (Thai, for the screen / the assistant). */
    const char* setSource(vehicle::SourceId s);
    void setKind(vehicle::Kind k);
    void setShiftRpm(int rpm);
    void setRpmMax(int rpm);
    /** Forget the saved BLE adapter: the next connect scans again. */
    void forgetBleAdapter();
    void setStyle(vehicle::DashStyle s);
    /** UART speed of the wired ELM327 (elm = true) or of the text source. */
    void setBaud(bool elm, uint32_t baud);

    vehicle::SourceId source() { return settings().source; }
    bool enabled() { return source() != vehicle::SourceId::Off; }
    /** Latest readings (a copy). */
    vehicle::Snapshot snapshot();
    /** "LIVE", "SIM", "K WAKE", "BT SCAN", "ELM INIT", "NO ECU", ... */
    String status();
    /** OBD over BLE was chosen but the BLE memory was given back at boot: restart once. */
    bool needsRestart();
    /** Honda K-Line: the last raw table ("" until one arrived). */
    String klineDump();

    /** SimTouch: the finger's throttle 0..1 (released = 0). */
    void setTouchThrottle(float t) { _touch = t; }

    /** The dash screen asks for itself to open / close (voice, console); taken by the UI task. */
    void requestOpen() { _openRequest = true; }
    void requestClose() { _closeRequest = true; }
    bool takeOpenRequest() { return _openRequest ? (_openRequest = false, true) : false; }
    bool takeCloseRequest() { return _closeRequest ? (_closeRequest = false, true) : false; }

    /** Status + readings + warning levels, for the `vehicle` tool and the console. */
    void describe(JsonObject out);

    /**
     * Warning worth saying out loud (coolant critical, not charging...) - at most one per kind every
     * 5 minutes. Fills `msg` (Thai) and returns true. Call from the agent loop.
     */
    bool takeAlert(String& msg, uint32_t now);

    static bool bleKeptAtBoot();

private:
    SemaphoreHandle_t _lock = nullptr;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    Settings _s;
    TaskHandle_t _task = nullptr;
    char _dir[48] = "";
    hondak::Layout _layout;
    vehicle::Snapshot _shared;
    char _status[24] = "";
    char _dump[150] = "";
    volatile bool _restart = false;
    volatile float _touch = 0;
    volatile bool _openRequest = false, _closeRequest = false;
    vehicle::DashModel _alertModel;
    uint32_t _alertAt[3] = {0, 0, 0};
    bool _alertActive[3] = {false, false, false};

    void save();
    void startTask();
    static void taskEntry(void* self);
    void loop();
    void publish(const vehicle::Snapshot& s, const char* status);
    void setStatus(const char* st);
    // source runners: return when the source changes (_restart)
    void runSim(bool touch);
    void runKline(VehicleLink& link);
    void runElm(VehicleLink& link, bool ble);
    void runText(VehicleLink& link);
    void runPhone();
};

extern Vehicle gVehicle;
