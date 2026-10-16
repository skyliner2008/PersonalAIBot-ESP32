/**
 * @file OtaUpdater.h
 * @brief Over-the-air firmware updates from GitHub Releases, with automatic rollback
 * @see wiki/13-OTA-Update.md
 *
 *   check    GET api.github.com/repos/<OTA_REPO>/releases/latest -> tag + "*-app.bin" (+ ".sha256")
 *   install  download the app image straight into the other OTA slot (settings, memory and the SD card
 *            are not touched), verify it (ESP32-S3 image header, the "PAB-ESP32-FW:<version>" marker =
 *            the release's version, sha256 = the published one, esp_ota_end's own image check), switch
 *            the boot slot, restart through the agent (clean Live close + memory flush)
 *   confirm  the new image boots as PENDING_VERIFY; it is marked valid once it has run 60 s with the
 *            agent loop alive. Any reset before that (crash, watchdog, power) makes the bootloader go
 *            back to the previous firmware - reported on the screen after that boot.
 * Nothing is installed without the user asking (menu / console / PC app); a daily check only tells.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <functional>

#include "../util/OtaLogic.h"

class OtaUpdater {
public:
    enum class State : uint8_t { Idle, Checking, UpToDate, Available, Downloading, Ready, Error };

    /** Boot: find out whether this image is a fresh update (pending verification) or a rollback. */
    void begin();

    bool requestCheck();    // async; false if busy
    bool requestInstall();  // async; needs State::Available
    /** Diagnostic: download a URL through the same verified TLS + redirects, hash only (nothing written). */
    bool requestFetch(const char* url);
    /** Diagnostic: copy the running firmware into the other slot and boot it (tests switch/confirm/rollback). */
    bool requestSelfTest();
    /** Boot the other slot's firmware (the one before the last update) if it is a valid image. */
    bool rollbackNow(String& why);

    /** Called by the agent loop: confirmation needs it alive. */
    void heartbeat() { _heartbeat = millis(); }

    State state() const { return _state; }
    const char* stateName() const;
    bool busy() const { return _state == State::Checking || _state == State::Downloading || _state == State::Ready; }
    /** Downloading / installing: the agent keeps the Live session closed meanwhile. */
    bool installing() const { return _state == State::Downloading || _state == State::Ready; }
    uint8_t progress() const { return _progress; }
    String latest() const;
    String error() const;
    bool pendingVerify() const { return _pendingVerify; }
    void describe(JsonObject out) const;

    // wiring (main.cpp): quiet the agent before a download, restart cleanly, show a line on the screen
    std::function<void()> onPrepare;
    std::function<void()> onReboot;
    std::function<void(const char* text, uint32_t holdMs)> onNotice;

private:
    enum Job : uint8_t { JobNone, JobCheck, JobInstall, JobFetch, JobSelfTest };

    TaskHandle_t _task = nullptr;
    SemaphoreHandle_t _lock = nullptr;
    volatile Job _job = JobNone;
    volatile State _state = State::Idle;
    volatile uint8_t _progress = 0;
    volatile uint32_t _heartbeat = 0;
    bool _pendingVerify = false;
    bool _silentCheck = false;  // the daily check: only a notice if something new
    uint32_t _lastCheck = 0;
    bool _checkedOnce = false;
    ota::Release _release;
    char _error[192] = "";  // Thai: 3 bytes a character (96 cut a message mid-character)
    char _fetchUrl[256] = "";

    static void taskEntry(void* self);
    void loop();
    void confirmIfHealthy();
    void dailyCheck();
    void runCheck();
    void runInstall();
    void runFetch();
    void runSelfTest();
    bool fetchText(const char* url, String& out, int& code);
    void fail(const char* fmt, ...);
    void notice(const char* text, uint32_t holdMs);
    void setRelease(const ota::Release& r);
};

extern OtaUpdater gOta;
