/**
 * @file PhoneLink.h
 * @brief The phone app (ESP32-AI Flasher) as a source of GPS and Google Maps steps, and a target for "navigate to"
 * @see wiki/18-Phone-Link-Navigation-Plan.md, src/phone/PhoneProtocol.h (the messages)
 *
 * Two transports, the same lines:
 *  - USB: the app is on the serial console; it sends "phone {...}" lines, the board answers "PHONE {...}".
 *    Plugging in pairs the phone (the app sends its token; saved in NVS "phone").
 *  - Wi-Fi: the app listens on TCP 47800. In a car the board rides the phone's hotspot, so the phone is the
 *    gateway; on a shared Wi-Fi the app announces itself on UDP 47801. The board connects, both sides prove
 *    the paired token, then the same lines flow. Only phone messages are accepted that way - never console
 *    commands (`set key`, `reboot` ...).
 * Nothing runs until a phone was paired once over USB (no task, no sockets, no tool for a board without one).
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "PhoneProtocol.h"

class PhoneLink {
public:
    enum class Via : uint8_t { None, Usb, Wifi };

    /** NVS pairing; starts the Wi-Fi task when a phone was paired. Call once at boot. */
    void begin();

    /** The JSON of a "phone {...}" line. `via` = where it came from. Console: Usb; the Wi-Fi task: Wifi. */
    void onLine(const char* json, Via via);

    bool paired();
    /** Forget the phone (console `phone unpair`): no Wi-Fi link, no tool after the next session. */
    void unpair();

    /** A message arrived within kLinkTimeoutMs. */
    bool connected();
    Via via();
    static const char* viaName(Via v) { return v == Via::Usb ? "usb" : v == Via::Wifi ? "wifi" : "none"; }

    phone::Gps gps();
    phone::Nav nav();
    String place();

    /**
     * Ask the phone: "navigate" (to = destination as spoken) or "stop". Returns the request id,
     * 0 = no phone connected. The phone answers later with a result (a failure is said by takeSay()).
     */
    int request(const char* what, const char* to = nullptr);

    /** Something the assistant should say (the next turn, a failure from the phone). Agent loop. */
    bool takeSay(String& msg, bool& turn, uint32_t now);

    /** Navigation started / ended since the last call (the UI opens / closes its screen). */
    bool takeNavStarted() { return _navStarted ? (_navStarted = false, true) : false; }
    bool takeNavEnded() { return _navEnded ? (_navEnded = false, true) : false; }

    /** Status for `diag`, the console and the `navigate` tool (no coordinates: those stay on the board). */
    void describe(JsonObject out);

private:
    static constexpr size_t kOutLen = 200;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    char _token[48] = "";
    phone::Gps _gps;
    phone::Nav _nav;
    char _place[128] = "";
    uint32_t _lastUsb = 0, _lastWifi = 0;
    Via _via = Via::None;
    int _nextId = 1;
    char _failure[160] = "";
    volatile bool _navStarted = false, _navEnded = false;
    phone::TurnAnnouncer _turns;
    QueueHandle_t _out = nullptr;  // lines for the Wi-Fi task to send
    TaskHandle_t _task = nullptr;
    volatile bool _wifiUp = false;

    void startTask();
    static void taskEntry(void* self);
    void loop();
    void sendLine(const char* json);
    void savePairing(const char* token);
};

extern PhoneLink gPhone;
