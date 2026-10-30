/**
 * @file UiTypes.h
 * @brief Types shared between the agent and the touch UI
 */

#pragma once

#include <vector>

#include <Arduino.h>
#include <stdint.h>

enum AvatarState : uint8_t {
    AVATAR_BOOT,
    AVATAR_CONNECTING,
    AVATAR_IDLE,
    AVATAR_LISTENING,
    AVATAR_THINKING,
    AVATAR_SPEAKING,
    AVATAR_ERROR,
    AVATAR_SLEEP,
    AVATAR_SETUP  // Wi-Fi / API key missing
};

/** Snapshot the agent publishes for the status bar (copied under a lock). */
struct UiStatus {
    bool wifi = false;
    int rssi = 0;
    int battery = -1;          // -1 = USB / no battery
    bool muted = false;
    bool configured = true;    // Wi-Fi + API key present
    uint32_t memories = 0;
    uint16_t reminders = 0;
    uint16_t openTasks = 0;
    uint32_t tokensPerMin = 0;
    char clock[6] = "";
    char radio[160] = "";      // "station - now playing" while the Internet radio is on
    // Weather snapshot for screensaver clock themes
    bool wxValid = false;
    float wxTemp = 0.0f;
    int16_t wxHumidity = 0;
    int16_t wxPm25 = -1;
    int8_t wxCode = 0;
    bool wxIsDay = true;
    char wxCity[48] = "";
    uint16_t priceAlerts = 0;  // T141: stored price alerts (the bell shows when this + reminders > 0)
};

/** T141: one alert on the alerts screen (a copy the agent publishes: the UI never touches the scheduler). */
struct UiAlert {
    enum Kind : uint8_t { Reminder, Timer, Price };
    Kind kind = Reminder;
    uint32_t id = 0;
    uint32_t due = 0;     // reminder / timer: unix epoch
    uint8_t repeat = 0;   // sched::Repeat
    char label[96] = "";  // reminder text / price alert name
    double target = 0;    // price alert: the price it waits for
    bool above = true;    // price alert: rises to (true) / falls to
};

/** T141: a change made on the alerts screen, done by the agent task. */
struct UiAlertEdit {
    enum Op : uint8_t { Remove, Update };
    Op op = Remove;
    UiAlert::Kind kind = UiAlert::Reminder;
    uint32_t id = 0;
    uint32_t due = 0;     // Update reminder: 0 = keep
    uint8_t repeat = 0;
    char label[96] = "";  // Update reminder: "" = keep
    double target = 0;    // Update price alert
};

/** Actions the UI asks the agent to perform (implemented by AgentController). */
class UiHost {
public:
    virtual ~UiHost() = default;
    virtual void uiTap() = 0;           // stop speaking / toggle mic / wake up / retry
    virtual void uiToggleMic() = 0;     // mic shortcut button
    virtual void uiApiTest() = 0;       // API-key self-test (result shown on the face)
    virtual void uiRestart() = 0;       // close the Live session cleanly, save, reboot
    virtual void uiLongPress() = 0;     // fresh session
    virtual void uiNewSession() = 0;
    virtual void uiClearMemory() = 0;   // facts + graph
    virtual void uiClearHistory() = 0;  // episodic log
    virtual void uiSystemInfo(String& out) = 0;
    virtual AvatarState uiState() = 0;
    virtual void uiActivity() {}        // touch on a non-face screen (radio player): delay the power-save clock
    virtual void uiPowerSave() {}       // preview power-save clock screen now
    /** T141: the alerts (reminders, timers, price alerts) as last published; returns their version. */
    virtual uint32_t uiAlerts(std::vector<UiAlert>& out) {
        out.clear();
        return 0;
    }
    virtual uint32_t uiAlertsVersion() { return 0; }
    virtual void uiAlertEdit(const UiAlertEdit& e) { (void)e; }
};
