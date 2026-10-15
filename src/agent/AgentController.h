/**
 * @file AgentController.h
 * @brief The personal-assistant "brain" (agent task, core 1)
 * @see wiki/04-System-Architecture.md
 * @see wiki/09-Assistant-Profiles-and-Tools.md
 *
 *   - Wi-Fi / NTP / Gemini Live session lifecycle with automatic model fallback
 *   - setup message from Settings: persona (name, voice, gender, personality),
 *     core memories, recent turns, open tasks, tools per model
 *   - audio routing: Gemini -> speaker, mic -> local VAD -> Gemini (half duplex)
 *   - sleep after silence (session closed, no tokens) and wake on voice / touch
 *   - timers & reminders that ring and are announced by the assistant
 *   - tool execution, episodic logging, background memory consolidation
 *   - derived state + status for the eyes UI and the LED
 */

#pragma once

#include <Arduino.h>

#include <atomic>
#include <vector>

#include "../ai/GeminiLiveClient.h"
#include "../ai/ModelFallback.h"
#include "../ai/TextModelClient.h"
#include "../app/AppConfig.h"
#include "../app/Settings.h"
#include "../audio/AudioManager.h"
#include "../memory/AsyncFileSink.h"
#include "../memory/EpisodicLog.h"
#include "../memory/MemoryStore.h"
#include "../system/Storage.h"
#include "../ui/UiTypes.h"
#include "AgentTools.h"
#include "Consolidation.h"
#include "Scheduler.h"
#include "TaskList.h"
#include "WebTools.h"

class FaceScreen;

class AgentController : public GeminiListener, public DeviceOps, public UiHost {
public:
    AgentController();

    bool begin(AudioManager* audio, FaceScreen* face, Storage* storage);
    void startTask();

    /** Run a tool inside the agent task (serial console). */
    bool runTool(const char* name, const char* argsJson, String& resultJson, uint32_t timeoutMs = 5000);
    void requestWake();
    void requestRetry() { post(CMD_RETRY); }  // reconnect now (after a pause / auth error)
    /** Offer Google Search again now (it is dropped for 6 h after a grounding-quota error). */
    void retrySearch() { post(CMD_SEARCH_RETRY); }
    /** Console / test: sleep now and switch to the dim clock face. */
    void requestPowerSave() { post(CMD_POWER_SAVE); }
    bool powerSave() const { return _powerSave; }
    /** Send a typed message to the assistant (serial "ask" / PC app chat). Thread-safe. */
    bool ask(const char* text);
    /** Diagnostic: how the Live client passes the key (URL ?key= or x-goog-api-key header). */
    void setKeyInHeader(bool on) { post(on ? CMD_KEY_HEADER : CMD_KEY_QUERY); }
    /** Diagnostic: 0 = full setup, 1 = without Google Search, 2 = minimal (model + audio only). */
    void setSetupMode(uint8_t mode) {
        _requestedSetupMode = mode;
        post(CMD_SETUP_MODE);
    }
    /** Function declarations as JSON (serial console / PC app). */
    void describeTools(String& out);

    // ---- Read-only status for other tasks ----
    AvatarState state() const { return (AvatarState)_state.load(); }
    bool micMuted() const { return _muted; }
    bool wifiConnected() const { return _wifiUp; }
    bool sleeping() const { return _sleeping; }
    bool ledOverride(uint32_t& rgb) const;
    void setBattery(int percent) { _battery = percent; }

    // ---- GeminiListener ----
    void buildSetup(JsonObject setup) override;
    void onSetupComplete() override;
    void onDisconnected(bool wasReady) override;
    void onServerClose(int code, const char* reason) override;
    void onAudio(const int16_t* pcm, size_t samples) override;
    void onInputTranscript(const char* text) override;
    void onOutputTranscript(const char* text) override;
    void onInterrupted() override;
    void onGenerationComplete() override;
    void onTurnComplete() override;
    void onToolCall(JsonArrayConst calls) override;
    void onToolCallCancellation(JsonArrayConst ids) override;
    void onSessionHandle(const char* handle, bool resumable) override;
    void onGoAway(uint32_t msLeft) override;
    void onUsage(uint32_t totalTokens) override;

    // ---- DeviceOps (tools) ----
    uint32_t nowEpoch() override;
    bool setLedColor(const char* color) override;
    int setVolume(int level) override;
    int setBrightness(int level) override;
    int volume() override;
    int brightness() override;
    int micGain() override;
    void configureDevice(const DeviceChange& change, JsonObject result) override;
    void fillStatus(JsonObject out) override;
    bool setExpression(const char* emotion) override;
    void requestSleep() override { _sleepPending = true; }
    void requestMicMute() override { _mutePending = true; }
    bool stopRadio() override;
    const char* playRadio(const char* station, JsonObject result) override;
    bool applyAssistantChange(const AssistantChange& change, JsonObject result) override;
    void describeAssistant(JsonObject out) override;

    // ---- UiHost (touch UI) ----
    void uiTap() override { post(CMD_TAP); }
    void uiToggleMic() override { post(CMD_TOGGLE_MIC); }
    void uiApiTest() override { post(CMD_API_TEST); }  // API-key self-test (menu / console / PC app)
    void uiRestart() override { post(CMD_RESTART); }
    /** Record what the mic sends to Gemini (after AGC) for `sec` seconds; dumped as RECINFO + RECORD. */
    void requestRecording(uint8_t sec) {
        _recRequestSec = sec;
        post(CMD_RECORD);
    }
    void uiLongPress() override { post(CMD_NEW_SESSION); }
    void uiNewSession() override { post(CMD_NEW_SESSION); }
    void uiClearMemory() override { post(CMD_CLEAR_MEMORY); }
    void uiClearHistory() override { post(CMD_CLEAR_HISTORY); }
    void uiSystemInfo(String& out) override;
    AvatarState uiState() override { return state(); }
    void uiActivity() override { _sleepSince = millis(); }  // asleep under the radio player: restart the clock-face timer

private:
    enum Command : uint8_t {
        CMD_TAP,
        CMD_TOGGLE_MIC,
        CMD_NEW_SESSION,
        CMD_CLEAR_MEMORY,
        CMD_CLEAR_HISTORY,
        CMD_SLEEP,
        CMD_WAKE,
        CMD_RETRY,
        CMD_API_TEST,
        CMD_KEY_QUERY,
        CMD_KEY_HEADER,
        CMD_SETUP_MODE,
        CMD_RESTART,
        CMD_RECORD,
        CMD_SEARCH_RETRY,
        CMD_POWER_SAVE
    };
    enum class SetupMode : uint8_t { Full, NoSearch, Minimal };  // diagnostic setup variants

    struct ToolRequest {
        const char* name;
        const char* args;
        String* result;
        SemaphoreHandle_t done;
    };

    /** Small PCM ring for pre-roll / wake buffering. */
    struct PcmRing {
        PsVector<int16_t> buf;
        size_t head = 0, count = 0;
        void init(size_t capacity) { buf.assign(capacity, 0); }
        void clear() { head = count = 0; }
        void push(const int16_t* s, size_t n);
        void trimTo(size_t maxSamples);
        size_t pop(int16_t* out, size_t n);
    };

    AudioManager* _audio = nullptr;
    FaceScreen* _face = nullptr;
    Storage* _storage = nullptr;

    ltm::MemoryStore _memory;
    ltm::EpisodicLog _episodes;
    sched::Scheduler _scheduler;
    todo::TaskList _tasks;
    AsyncFileSink _sink;
    AgentTools* _tools = nullptr;
    GeminiLiveClient _gemini;
    TextModelClient _text;
    webtools::WebTools _web;
    uint32_t _sessionGen = 1;                       // +1 per setupComplete (web-tool answers are per session)
    static constexpr uint32_t kConsoleTag = 0;      // web-tool call from the serial console / PC app
    fallback::ModelSelector _liveSel{(size_t)LiveModel::Count};
    fallback::ModelSelector _textSel{2};
    consolidate::RateLimiter _consolidateLimit;
    QueueHandle_t _commands = nullptr;
    QueueHandle_t _toolRequests = nullptr;
    QueueHandle_t _askQueue = nullptr;  // char* (PSRAM) typed messages
    SemaphoreHandle_t _infoLock = nullptr;
    String _info;

    // Settings
    SettingsData _cfg;
    uint32_t _cfgVersion = 0;
    bool _configOk = false;

    // Connectivity / session
    bool _wifiStarted = false;
    volatile bool _wifiUp = false;
    uint32_t _wifiBeginAt = 0;
    uint32_t _wifiRetryMs = 3000;
    uint8_t _wifiFailures = 0;
    bool _wifiErrorShown = false;
    bool _timeStarted = false, _timeValidSeen = false;
    uint8_t _activeModel = 0;
    String _resumeHandle;
    uint8_t _failedResumes = 0;
    bool _reconnectPending = false;  // goAway: resume after the current reply
    bool _restartPending = false;    // persona/model changed: fresh session after the reply
    uint32_t _restartAt = 0;         // ... and once the settings stopped changing (PC app sends several)
    bool _sleepPending = false;      // go_to_sleep: after the reply
    bool _fatalError = false;
    String _errorText;
    uint32_t _holdUntil = 0;  // monoSec: every live model failed, wait before reconnecting
    bool _closeSeen = false;  // this connection ended with a server close frame
    String _lastClose;        // "1011 quota: You exceeded ..." for the info screen
    String _lastProbe;        // summary of the last API-key self-test
    SetupMode _setupMode = SetupMode::Full;
    volatile uint8_t _requestedSetupMode = 0;
    bool _searchInSetup = false;     // the current/last setup offered googleSearch
    bool _vadTuningInSetup = false;  // ... and realtimeInputConfig.automaticActivityDetection
    bool _vadTuningRejected = false; // a model refused it: leave it out from now on (until reboot)
    uint32_t _searchOffUntil = 0;    // monoSec: Google Search quota used up, sessions go without it
    static constexpr uint32_t kSearchOffSec = 6 * 3600;

    // Conversation
    volatile bool _muted = false;
    volatile bool _sleeping = false;
    volatile bool _powerSave = false;   // asleep + clock face + dimmed backlight
    bool _powerSaveAfterSleep = false;   // configure_device power_save_now
    volatile bool _mutePending = false;  // mute_microphone: after the current reply
    uint32_t _sleepSince = 0;
    bool _dropAudio = false;
    uint32_t _dropSince = 0;
    bool _awaitingReply = false;
    uint32_t _lastUserSpeech = 0, _lastVoice = 0, _lastActivity = 0, _lastAgentAudio = 0;
    uint32_t _toolBusyUntil = 0;
    PsString _userTurn, _agentTurn;
    uint32_t _userTurnTs = 0, _agentTurnTs = 0;

    // Mic path
    bool _gateAllowed = false;
    bool _vadOpen = false;
    uint8_t _onset = 0, _wakeVotes = 0;
    float _radioRef = 0.0f, _coupling = 0.0f;  // radio echo estimate for waking by voice over music
    uint32_t _radioSettleUntil = 0;
    float _noise = 0.002f;
    float _threshold = AGENT_VAD_MIN_LEVEL;
    bool _wakeBuffering = false;
    PcmRing _preroll;
    float _agcGain = 1.0f;
    uint32_t _followUpUntil = 0;  // after a reply: stream without waiting for an onset
    // per-utterance stats for the [Mic] log
    uint32_t _uttStart = 0, _uttChunks = 0, _uttVoiced = 0;
    float _uttPeak = 0;
    bool _uttFollowUp = false;
    uint8_t _blipChunks = 0;
    float _blipPeak = 0;
    uint32_t _lastBlipLog = 0;
    uint32_t _replyWatchAt = 0;  // speech sent at; cleared by any answer
    bool _heardSinceSend = false;
    // diagnostic recording ("rec" console command / PC app)
    PsVector<int16_t> _rec;
    PsVector<uint8_t> _recFlags;  // per 40-ms chunk: bit0 voice, bit1 sent to Gemini
    size_t _recLen = 0;
    bool _recActive = false;
    uint32_t _recDeadline = 0;
    volatile uint8_t _recRequestSec = 0;

    // Reminders
    std::vector<PsString> _announcements;
    bool _ringing = false;
    uint32_t _ringUntil = 0, _nextChime = 0, _nextSchedulerCheck = 0;

    // Consolidation
    uint32_t _turnsSinceConsolidation = 0;
    uint32_t _consolidatedUntil = 0, _pendingUntil = 0;
    uint8_t _consolidateModel = 0;

    // Token usage (6 x 10-s buckets)
    uint32_t _tokBucket[6] = {};
    uint32_t _tokBucketAt = 0;

    // Shared with other tasks
    std::atomic<uint8_t> _state{AVATAR_BOOT};
    std::atomic<uint32_t> _ledRgb{0};
    std::atomic<bool> _ledOverride{false};
    volatile int _battery = -1;
    uint32_t _nextStatus = 0, _nextInfo = 0;

    static void taskEntry(void* self);
    void loop();
    void post(Command c);

    void applySettings(bool initial);
    void handleCommands();
    void handleAsks();
    void handleWebResults();
    void handleToolRequests();
    void handleConnectivity();
    void handleAudioInput();
    void handleScheduler(uint32_t now);
    void handleSleepAndRestarts(uint32_t now);
    void handleConsolidation(uint32_t now);
    void deliverAnnouncements();
    void updateState(uint32_t now);
    void publishStatus();
    void updateInfo();

    void enterSleep();
    void enterPowerSave();
    void leavePowerSave();

    void handleRadio(uint32_t now, bool busy);
    bool _radioWasOn = false;
    void wake(bool byVoice);
    void startFreshSession(const char* hint);
    void enterHold(uint32_t sec);
    void retryNow();
    void toggleMic();
    void fillDiagnostics(JsonObject out);
    void startApiTest();
    bool searchAvailable() const;
    void handleProbeResult(int status, const PsString* report);
    void flushUserTurn();
    void flushAgentTurn(bool interrupted);
    void buildSystemPrompt(PsString& out);
    void sendPreroll();
    void sendMic(const int16_t* pcm, size_t n);  // AGC + upload
    void updateAgc(float level);
    void beginUtterance(uint32_t now, float level, bool followUp);
    void endUtterance(uint32_t now);
    void noteBlip(uint32_t now, bool voice, float level);
    void checkNoReply(uint32_t now);
    void markAnswered() { _replyWatchAt = 0; }
    void startRecording(uint8_t sec);
    void recordChunk(const int16_t* pcm, size_t n, bool voice, bool sent);
    void finishRecording();
    uint32_t tokensPerMinute() const;
    void noteActivity() { _lastActivity = millis(); }
    static uint32_t monoSec() { return millis() / 1000; }
    /** ms from t to now, 0 if t is later than now (stamped after `now` was sampled). */
    static uint32_t sinceMs(uint32_t now, uint32_t t) { return (int32_t)(now - t) > 0 ? now - t : 0; }
};
