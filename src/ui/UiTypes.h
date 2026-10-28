/**
 * @file UiTypes.h
 * @brief Types shared between the agent and the touch UI
 */

#pragma once

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
};
