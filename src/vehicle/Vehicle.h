/**
 * @file Vehicle.h
 * @brief Vehicle dash back end: the active data source in its own task, its settings, and the shared readings
 * @see wiki/16-Vehicle-Dash.md
 *
 * Sources (vehicle::SourceId): built-in simulator (driving by itself / finger = throttle), Honda K-Line,
 * OBD-II ELM327 over Bluetooth LE or a UART wire, text lines from another board. The UART ones use the
 * UART header (GPIO 43 TX, 44 RX), so not together with a device on those pins (agent/devices.json).
 *
 * Bluetooth (only built with -DVEHICLE_BLE=1, see VehicleData.h / T078): Arduino frees the BLE controller memory (~36 KB internal RAM) at boot unless something says
 * BLE is in use. bleInUse() (here) says so only when the saved source is OBD over BLE - picking that
 * source therefore takes one restart, and everything else keeps the RAM it had.
 *
 * Settings live in NVS "veh"; agent/vehicle.json may override the Honda K-Line table layout and give the
 * engine size / fuel ({"engine_l": 1.2, "fuel": "diesel"}) for the fuel estimate.
 * The task exists only once a source other than "off" was chosen (no cost for a board without a car).
 *
 * T112: besides the readings the task publishes the source's diagnosis (OBD-II trouble codes, VIN, readiness -
 * vehicle::obd::Diag) and the health analysis + trip (vehicle::Health, worked out in the task for every
 * source). The wired ELM327 learns its protocol (tried first next time) and baud rate (tries 38400, 9600,
 * 115200 ... while the adapter stays silent); service() saves them from a task with an internal stack.
 * A source without a speed (most motorcycle ECUs) takes the phone's GPS speed when the phone app sends it.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "DashModel.h"
#include "HondaKline.h"
#include "Obd.h"
#include "VehicleData.h"
#include "VehicleHealth.h"

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
        uint8_t protocol = 0;       // the ELM327 protocol that found the car last time (0 = search)
        bool log = true;            // T115: the OBD log on the SD card (agent/obdlog)
        char replayFile[48] = "";   // the log the replay source plays ("" = the newest)
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
    /** T115: record the real sources on the SD card (VehicleLog). */
    void setLogEnabled(bool on);
    /** Play a recorded log ("latest" / "" = the newest) at `speed` x real time: the replay source. nullptr = ok,
     *  else why not (Thai). */
    const char* startReplay(const char* name, float speed = 1.0f);

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

    /** Show the dash (voice, the face shortcut). With no source chosen yet the simulator starts, so there is
     *  something to see: returns true when it did. */
    bool openDash();
    /** The dash screen asks for itself to open / close (voice, console); taken by the UI task. */
    void requestOpen() { _openRequest = true; }
    void requestClose() { _closeRequest = true; }
    bool takeOpenRequest() { return _openRequest ? (_openRequest = false, true) : false; }
    bool takeCloseRequest() { return _closeRequest ? (_closeRequest = false, true) : false; }

    /** Status + readings + health + trip (+ trouble codes), for the `vehicle` tool and the console.
     *  full: everything a scan knows (freeze frame, readiness, VIN, protocol, pending / permanent codes). */
    void describe(JsonObject out, bool full = false);

    /** The source's diagnosis (a copy): trouble codes, VIN, readiness. available = false: it has none. */
    vehicle::obd::Diag diag();
    /** Findings, trends, battery and the trip (a copy). */
    vehicle::Health health();
    /** The speed shown comes from the phone's GPS (the source has none). */
    bool speedFromGps();
    /** Read the trouble codes now. Returns the diag().scanSeq that means "done", 0 = cannot now (no codes on
     *  this source, or not connected to the car). */
    uint32_t requestScan();
    /** Clear the trouble codes: nullptr = asked (`seq` as requestScan), else why not (Thai). */
    const char* requestClear(uint32_t& seq);
    bool scanDone(uint32_t seq);
    /** Start a new trip (distance, fuel used). */
    void resetTrip() { _tripReset = true; }

    /** A dash page is on screen (the UI calls it every frame): its readings are polled fast. Nobody calls
     *  it for 2 s = dash closed: only what the health analysis needs, slowly. */
    void noteDashVisible(bool infoPage) {
        const uint32_t now = millis();
        const bool back = !_visibleAt || (int32_t)(now - _visibleAt) > 1000;  // just opened: Bluetooth OBD starts now (T114)
        _visibleInfo = infoPage;
        _visibleAt = now | 1;
        if (back && _task) xTaskNotifyGive(_task);
    }

    /** ~1 Hz from a task with an internal stack (the agent): saves what the source learned (protocol, baud). */
    void service();

    /**
     * Something worth saying out loud: a new trouble code (once each), or a health finding (engine overheating,
     * not charging, temperature rising fast, battery low, fuel low, lean / rich...) at most every few minutes
     * each. Fills `msg` (Thai) and returns true. Call from the agent loop.
     */
    bool takeAlert(String& msg, uint32_t now);

    static bool bleKeptAtBoot();

private:
    struct Shared;  // in PSRAM: the readings, diagnosis and health the task publishes
    SemaphoreHandle_t _lock = nullptr;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    Settings _s;
    TaskHandle_t _task = nullptr;
    char _dir[48] = "";
    hondak::Layout _layout;
    Shared* _sh = nullptr;
    vehicle::HealthMonitor* _hm = nullptr;  // PSRAM, used by the vehicle task only
    char _status[24] = "";
    char _dump[150] = "";
    volatile bool _restart = false;
    volatile float _touch = 0;
    volatile bool _openRequest = false, _closeRequest = false;
    volatile bool _scanReq = false, _clearReq = false, _tripReset = false;
    volatile bool _visibleInfo = false;
    volatile uint32_t _visibleAt = 0;
    volatile uint8_t _learnedProto = 0;
    volatile uint32_t _learnedBaud = 0;
    volatile bool _learnedBle = false;
    float _engineL = 0;   // agent/vehicle.json (0 = the kind's default)
    bool _diesel = false;
    uint32_t _healthAt = 0, _lastInSeq = 0, _scanCount = 0;
    uint32_t _gpsAt = 0, _gpsCheckAt = 0;
    bool _speedFromGps = false;
    uint32_t _alertAt[(int)vehicle::Finding::Count] = {};
    vehicle::obd::DtcList _announced;  // trouble codes already said (once each per boot)

    void save();
    void startTask();
    static void taskEntry(void* self);
    void loop();
    void publish(const vehicle::Snapshot& s, const char* status);
    void publishDiag(const vehicle::obd::Diag& d);
    void setStatus(const char* st);
    vehicle::HealthConfig healthConfig();
    /** 0 Background, 1 Gauge, 2 Info (Elm327::Focus). */
    uint8_t focus();
    void fillSpeedFromPhone(vehicle::Snapshot& s, uint32_t now);
    enum class ElmKind : uint8_t { Uart, Ble, Replay };
    void runElm(VehicleLink& link, ElmKind kind);
    void runReplay();
    /** A new SD log file for the current source (+ what it is). */
    void logStart();
    void logTick(const vehicle::Snapshot& s, const char* status, uint32_t now, bool healthNew);
    // T115: what the log has had (the vehicle task)
    char _logStatus[24] = "";
    uint32_t _logValuesAt = 0, _logHealth = 0, _logDiag = 0;
    // the replay source's position (for describe)
    char _replayName[48] = "";
    volatile uint32_t _replayAt = 0, _replayLen = 0;
    float _replaySpeed = 1.0f;
    // source runners: return when the source changes (_restart)
    void runSim(bool touch);
    void runKline(VehicleLink& link);
    void runText(VehicleLink& link);
    void runPhone();
};

extern Vehicle gVehicle;
