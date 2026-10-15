/**
 * @file AgentTools.h
 * @brief Function-calling tools of the personal assistant (memory, time, tasks, persona, device)
 * @see wiki/09-Assistant-Profiles-and-Tools.md
 * @see wiki/08-Long-Term-Memory-GraphRAG.md
 *
 * Declarations are sent in every setup message and are billed on every turn,
 * so descriptions are short and in English (cheaper to tokenize than Thai).
 *
 * Tool behaviour depends on the Live model:
 *   gemini-3.8-live                    -> BLOCKING (set_expression NON_BLOCKING + SILENT)
 *   gemini-3.8-live-extended-thinking  -> NON_BLOCKING only (BLOCKING is a hard error)
 *   gemini-3.1-flash-live-preview      -> field omitted (sequential calls only)
 */

#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include "../memory/EpisodicLog.h"
#include "../memory/MemoryStore.h"
#include "Scheduler.h"
#include "TaskList.h"

enum class ToolBehavior : uint8_t { Blocking, NonBlocking, Unspecified };

/** Validated request to change the assistant (all fields optional). */
struct AssistantChange {
    const char* profile = nullptr;      // preset id
    const char* voice = nullptr;        // Gemini voice name
    int personality = -1;               // index in persona::kPersonalities
    int gender = -1;                    // persona::Gender
    const char* agentName = nullptr;
    const char* userName = nullptr;
    int eyeColor = -1;                  // index in persona::kEyeColors
    bool any() const {
        return profile || voice || personality >= 0 || gender >= 0 || agentName || userName || eyeColor >= 0;
    }
};

/** Validated device settings change from configure_device (-1 / false = unchanged). */
struct DeviceChange {
    int micGain = -1;          // ES8311 mic sensitivity 0-7
    int sleepMinutes = -1;     // silence before sleeping (0 = never)
    int clockMinutes = -1;     // asleep this long -> clock face + dim screen (0 = never)
    int dimBrightness = -1;    // brightness % in power save
    bool powerSaveNow = false; // go to sleep after this reply and show the clock right away
    bool any() const {
        return micGain >= 0 || sleepMinutes >= 0 || clockMinutes >= 0 || dimBrightness >= 0 || powerSaveNow;
    }
};

/** Hardware / app actions the tools can trigger (implemented by the firmware, mocked in tests). */
class DeviceOps {
public:
    virtual ~DeviceOps() = default;
    virtual uint32_t nowEpoch() = 0;                // 0 if the clock is not synced
    virtual bool setLedColor(const char* color) = 0;
    virtual int setVolume(int level) = 0;           // returns the applied level
    virtual int setBrightness(int level) = 0;       // returns the applied level
    virtual int volume() = 0;                       // current speaker volume 0-100
    virtual int brightness() = 0;                   // current screen brightness 5-100
    virtual int micGain() = 0;                      // current mic sensitivity 0-7
    /** Apply a validated change; fills `result` with the new values. */
    virtual void configureDevice(const DeviceChange& change, JsonObject result) = 0;
    virtual void fillStatus(JsonObject out) = 0;    // battery, Wi-Fi, storage, ...
    virtual bool setExpression(const char* emotion) = 0;
    virtual void requestSleep() = 0;                // after the current reply
    virtual void requestMicMute() = 0;              // mic off after the current reply (only the screen turns it on)
    virtual bool stopRadio() = 0;                   // true if the Internet radio was on
    /** Play from the station list: number, name or genre ("" = last station). nullptr = ok, else why not. */
    virtual const char* playRadio(const char* station, JsonObject result) = 0;
    virtual bool applyAssistantChange(const AssistantChange& change, JsonObject result) = 0;
    virtual void describeAssistant(JsonObject out) = 0;
};

class AgentTools {
public:
    AgentTools(ltm::MemoryStore& memory, ltm::EpisodicLog& episodes, sched::Scheduler& scheduler,
               todo::TaskList& tasks, DeviceOps& device, int32_t utcOffsetSec);

    /** Append {"googleSearch":{}} (optional) and {"functionDeclarations":[...]} to `tools`. */
    void addDeclarations(JsonArray tools, ToolBehavior behavior, bool googleSearch) const;

    /** Optional tool packs: finance (TradingView analysis / scan / calendar / alerts), MT5 (bridge configured), trading. */
    void setPacks(bool finance, bool mt5, bool mt5Trade) {
        _finance = finance;
        _mt5 = mt5;
        _mt5Trade = mt5Trade;
    }

    /**
     * Run one function call. Fills `response` with {"result": {...}} or {"error": "..."}.
     * For NON_BLOCKING set_expression on gemini-3.8-live the caller adds "scheduling":"SILENT".
     */
    bool execute(const char* name, JsonObjectConst args, JsonObject response);

    /** Tools that should never interrupt speech (declared NON_BLOCKING where allowed). */
    static bool isSilentTool(const char* name);

    uint32_t memoryWrites() const { return _memoryWrites; }

    static const char* const kLedColors[];

private:
    ltm::MemoryStore& _memory;
    ltm::EpisodicLog& _episodes;
    sched::Scheduler& _scheduler;
    todo::TaskList& _tasks;
    DeviceOps& _device;
    int32_t _utcOffset;
    bool _finance = true, _mt5 = false, _mt5Trade = false;
    uint32_t _memoryWrites = 0;

    using Handler = bool (AgentTools::*)(JsonObjectConst, JsonObject, const char*&);
    struct Entry {
        const char* name;
        Handler fn;
    };
    static const Entry kHandlers[];

    bool saveMemory(JsonObjectConst a, JsonObject r, const char*& e);
    bool saveRelation(JsonObjectConst a, JsonObject r, const char*& e);
    bool recallMemory(JsonObjectConst a, JsonObject r, const char*& e);
    bool forgetMemory(JsonObjectConst a, JsonObject r, const char*& e);
    bool recallConversation(JsonObjectConst a, JsonObject r, const char*& e);
    bool setTimer(JsonObjectConst a, JsonObject r, const char*& e);
    bool setReminder(JsonObjectConst a, JsonObject r, const char*& e);
    bool listReminders(JsonObjectConst a, JsonObject r, const char*& e);
    bool cancelReminder(JsonObjectConst a, JsonObject r, const char*& e);
    bool addTask(JsonObjectConst a, JsonObject r, const char*& e);
    bool listTasks(JsonObjectConst a, JsonObject r, const char*& e);
    bool updateTask(JsonObjectConst a, JsonObject r, const char*& e);
    bool deviceStatus(JsonObjectConst a, JsonObject r, const char*& e);
    bool setExpression(JsonObjectConst a, JsonObject r, const char*& e);
    bool configureAssistant(JsonObjectConst a, JsonObject r, const char*& e);
    bool listAssistantOptions(JsonObjectConst a, JsonObject r, const char*& e);
    bool setVolume(JsonObjectConst a, JsonObject r, const char*& e);
    bool setBrightness(JsonObjectConst a, JsonObject r, const char*& e);
    bool configureDevice(JsonObjectConst a, JsonObject r, const char*& e);
    bool controlLed(JsonObjectConst a, JsonObject r, const char*& e);
    bool goToSleep(JsonObjectConst a, JsonObject r, const char*& e);
    bool muteMicrophone(JsonObjectConst a, JsonObject r, const char*& e);
    bool restScreen(JsonObjectConst a, JsonObject r, const char*& e);
    bool stopRadio(JsonObjectConst a, JsonObject r, const char*& e);
    bool playRadio(JsonObjectConst a, JsonObject r, const char*& e);
};
