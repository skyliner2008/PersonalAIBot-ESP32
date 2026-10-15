/**
 * @file AgentController.cpp
 * @brief Personal assistant orchestration (session, fallback, VAD, sleep, reminders, memory)
 * @see wiki/04-System-Architecture.md
 */

#include "AgentController.h"

#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "../app/AppConfig.h"
#include "../audio/RadioPlayer.h"
#include "../audio/RadioStations.h"
#include "../ui/FaceScreen.h"
#include "../ui/UiManager.h"
#include "../util/JsonAlloc.h"
#include "../util/TimeFmt.h"
#include "Personas.h"
#include "WebTools.h"

namespace {

constexpr size_t kChunk = AUDIO_UPLOAD_RATE * AUDIO_MIC_CHUNK_MS / 1000;  // 640 samples
constexpr size_t kPrerollSamples = AUDIO_UPLOAD_RATE * AGENT_VAD_PREROLL_MS / 1000;
constexpr size_t kWakeBufferSamples = AUDIO_UPLOAD_RATE * 2;  // 2 s spoken while the session connects
constexpr uint32_t kWifiRetryMinMs = 3000;   // back-off between Wi-Fi attempts: 3 s doubling to 60 s
constexpr uint32_t kWifiRetryMaxMs = 60000;   // (routers block clients that retry every second)
constexpr uint32_t kThinkingTimeoutMs = 12000;

std::atomic<uint8_t> gWifiReason{0};  // last STA disconnect reason (Wi-Fi event task)

const char* wifiReasonTh(uint8_t r) {
    switch (r) {
        case WIFI_REASON_AUTH_EXPIRE: return "เราเตอร์ปฏิเสธการเชื่อมต่อ";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "รหัสผ่านอาจไม่ถูกต้อง";
        case WIFI_REASON_NO_AP_FOUND: return "ไม่พบเครือข่าย";
        case WIFI_REASON_BEACON_TIMEOUT: return "สัญญาณอ่อน";
        case WIFI_REASON_ASSOC_TOOMANY: return "เราเตอร์รับเครื่องเพิ่มไม่ได้";
        case WIFI_REASON_TIMEOUT:
        case WIFI_REASON_CONNECTION_FAIL: return "เราเตอร์ไม่ตอบ";
        default: return "เชื่อมต่อไม่สำเร็จ";
    }
}

ltm::MemoryStore::Options memoryOptions() {
    ltm::MemoryStore::Options o;
    o.maxRecords = MEMORY_MAX_RECORDS;
    return o;
}

ltm::EpisodicLog::Options episodeOptions() {
    ltm::EpisodicLog::Options o;
    o.utcOffsetSec = TIME_UTC_OFFSET_SEC;
    return o;
}

float rms(const int16_t* s, size_t n) {
    float acc = 0;
    for (size_t i = 0; i < n; i++) {
        const float v = s[i] / 32768.0f;
        acc += v * v;
    }
    return n ? sqrtf(acc / n) : 0.0f;
}

uint16_t eyeColor565(uint8_t idx) {
    const persona::EyeColor& c = persona::kEyeColors[idx < persona::kEyeColorCount ? idx : 0];
    return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

ToolBehavior behaviorFor(uint8_t model) {
    switch ((LiveModel)model) {
        case LiveModel::Live38: return ToolBehavior::Blocking;
        case LiveModel::Live38Thinking: return ToolBehavior::NonBlocking;  // BLOCKING is a hard error
        default: return ToolBehavior::Unspecified;                         // 3.1: sequential calls only
    }
}

}  // namespace

// ============================================================
// PcmRing
// ============================================================

void AgentController::PcmRing::push(const int16_t* s, size_t n) {
    const size_t cap = buf.size();
    if (!cap) return;
    for (size_t i = 0; i < n; i++) {
        buf[(head + count) % cap] = s[i];
        if (count < cap) count++;
        else head = (head + 1) % cap;
    }
}

void AgentController::PcmRing::trimTo(size_t maxSamples) {
    if (count <= maxSamples) return;
    const size_t drop = count - maxSamples;
    head = (head + drop) % buf.size();
    count = maxSamples;
}

size_t AgentController::PcmRing::pop(int16_t* out, size_t n) {
    const size_t cap = buf.size();
    const size_t m = n < count ? n : count;
    for (size_t i = 0; i < m; i++) out[i] = buf[(head + i) % cap];
    head = (head + m) % (cap ? cap : 1);
    count -= m;
    return m;
}

// ============================================================
// Setup
// ============================================================

AgentController::AgentController()
    : _memory(memoryOptions()),
      _episodes(episodeOptions()),
      _scheduler(TIME_UTC_OFFSET_SEC),
      _consolidateLimit(CONSOLIDATE_MIN_INTERVAL_SEC, CONSOLIDATE_MAX_PER_DAY, TIME_UTC_OFFSET_SEC) {}

bool AgentController::begin(AudioManager* audio, FaceScreen* face, Storage* storage) {
    _audio = audio;
    _face = face;
    _storage = storage;
    _commands = xQueueCreate(12, sizeof(uint8_t));
    _toolRequests = xQueueCreate(2, sizeof(ToolRequest));
    _askQueue = xQueueCreate(4, sizeof(char*));
    _infoLock = xSemaphoreCreateMutex();
    _preroll.init(kWakeBufferSamples);

    // ---- Persistent state (memory, reminders, tasks) ----
    _sink.begin();
    if (_storage->kind() != StorageKind::None) {
        char path[96];
        const uint32_t t0 = millis();
        snprintf(path, sizeof(path), "%s/ltm.jsonl", _storage->agentDir());
        _memory.open(path, &_sink);
        _episodes.open(_storage->episodesDir(), &_sink, 0);
        snprintf(path, sizeof(path), "%s/reminders.json", _storage->agentDir());
        _scheduler.open(path);
        snprintf(path, sizeof(path), "%s/tasks.json", _storage->agentDir());
        _tasks.open(path);
        const ltm::Stats s = _memory.stats();
        Serial.printf("[Memory] %u facts, %u relations, %u reminders, %u tasks loaded in %lu ms (%s)\n",
                      (unsigned)s.facts, (unsigned)s.relations, (unsigned)_scheduler.items().size(),
                      (unsigned)_tasks.size(), (unsigned long)(millis() - t0), _storage->kindName());
    } else {
        _scheduler.open(nullptr);
        _tasks.open(nullptr);
        Serial.println("[Memory] No storage: memory lives in RAM until reboot");
    }
    _tools = new AgentTools(_memory, _episodes, _scheduler, _tasks, *this, TIME_UTC_OFFSET_SEC);

    _gemini.begin(this);
    _text.begin();
    _web.begin(_storage->kind() != StorageKind::None ? _storage->agentDir() : "");
    _cfgVersion = gSettings.version();
    applySettings(true);
    _lastActivity = millis();
    return true;
}

void AgentController::startTask() { xTaskCreatePinnedToCore(taskEntry, "agent", 12288, this, 4, nullptr, 1); }

void AgentController::taskEntry(void* self) { static_cast<AgentController*>(self)->loop(); }

bool AgentController::ask(const char* text) {
    if (!_askQueue || !text || !*text) return false;
    const size_t n = strlen(text);
    char* p = (char*)psramMalloc(n + 1);
    if (!p) return false;
    memcpy(p, text, n + 1);
    if (xQueueSend(_askQueue, &p, 0) != pdTRUE) {
        psramFree(p);
        return false;
    }
    return true;
}

// Typed message (serial "ask" / PC app): handled like a spoken user turn
void AgentController::handleAsks() {
    char* p;
    while (_askQueue && xQueueReceive(_askQueue, &p, 0) == pdTRUE) {
        noteActivity();
        if (_sleeping) wake(false);
        if (_holdUntil || _fatalError) retryNow();
        flushUserTurn();
        _episodes.append('u', p, nowEpoch());
        _face->pushTranscript('u', p);
        _announcements.emplace_back(p);  // sent by deliverAnnouncements() once the session is ready
        Serial.printf("[Agent] Typed message queued (%u bytes)\n", (unsigned)strlen(p));
        psramFree(p);
    }
}

void AgentController::post(Command c) {
    const uint8_t v = c;
    if (_commands) xQueueSend(_commands, &v, 0);
}

void AgentController::requestWake() { post(CMD_WAKE); }

bool AgentController::ledOverride(uint32_t& rgb) const {
    rgb = _ledRgb.load();
    return _ledOverride.load();
}

bool AgentController::runTool(const char* name, const char* argsJson, String& resultJson, uint32_t timeoutMs) {
    if (!_toolRequests) return false;
    ToolRequest req{name, argsJson, &resultJson, xSemaphoreCreateBinary()};
    if (!req.done) return false;
    if (xQueueSend(_toolRequests, &req, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        vSemaphoreDelete(req.done);
        return false;
    }
    xSemaphoreTake(req.done, portMAX_DELAY);  // the agent task writes into resultJson
    vSemaphoreDelete(req.done);
    return true;
}

// ============================================================
// Settings
// ============================================================

void AgentController::applySettings(bool initial) {
    const SettingsData n = gSettings.get();
    const bool wifiChanged = strcmp(n.wifiSsid, _cfg.wifiSsid) || strcmp(n.wifiPass, _cfg.wifiPass);
    const bool keyChanged = strcmp(n.apiKey, _cfg.apiKey) != 0;
    const bool sessionChanged = strcmp(n.voice, _cfg.voice) || n.personality != _cfg.personality ||
                                n.gender != _cfg.gender || strcmp(n.agentName, _cfg.agentName) ||
                                strcmp(n.userName, _cfg.userName) || n.model != _cfg.model ||
                                n.thinking != _cfg.thinking || n.googleSearch != _cfg.googleSearch ||
                                n.financeTools != _cfg.financeTools || n.mt5Trade != _cfg.mt5Trade ||
                                (n.mt5Url[0] != 0) != (_cfg.mt5Url[0] != 0);

    if (initial || n.volume != _cfg.volume) _audio->setVolume(n.volume);
    if (initial || n.micGain != _cfg.micGain) _audio->setMicGain(n.micGain);
    if ((initial || n.brightness != _cfg.brightness) && !_powerSave) gUi.setBrightness(n.brightness);
    if (_powerSave && n.dimBrightness != _cfg.dimBrightness) gUi.setBrightness(n.dimBrightness);
    if (initial || n.eyeColor != _cfg.eyeColor) _face->setEyeColor(eyeColor565(n.eyeColor));
    gUi.setTouchTransform(n.touchMap);
    _tools->setPacks(n.financeTools, n.mt5Url[0] != 0, n.mt5Url[0] != 0 && n.mt5Trade);
    _liveSel.setPreferred(n.model);
    _textSel.setPreferred(n.textModel);

    _cfg = n;
    _configOk = n.wifiSsid[0] && strlen(n.apiKey) > 20;

    if (initial) {
        _activeModel = n.model;
        _muted = n.micMuted;  // restored mute state (voice can only turn it off)
        _gemini.configure(n.apiKey);
        _text.setApiKey(n.apiKey);
        if (!_configOk) {
            Serial.println("[Agent] Not configured yet: set Wi-Fi and API key in the settings menu");
            _face->setSystemText("ยังไม่ได้ตั้งค่า แตะไอคอนเฟืองมุมขวาบน เพื่อใส่ Wi-Fi และ API Key", 0);
        }
        return;
    }
    if (wifiChanged) {
        Serial.printf("[Agent] Wi-Fi settings changed -> %s\n", n.wifiSsid);
        if (_gemini.started()) _gemini.stop();
        WiFi.disconnect();
        _wifiStarted = false;
        _wifiUp = false;
    }
    if (keyChanged) {
        Serial.println("[Agent] API key changed");
        _gemini.configure(n.apiKey);
        _text.setApiKey(n.apiKey);
        _resumeHandle = "";
        _fatalError = false;
        _errorText = "";
        _holdUntil = 0;  // a new key gets a clean slate
        _liveSel.reset();
        _textSel.reset();
    } else if (sessionChanged) {
        _restartPending = true;  // new voice/persona/model: fresh session after the current reply
        _restartAt = millis() + 1500;  // debounce: the PC app sends several `set` lines at once
        _resumeHandle = "";
    }
    if (_configOk && (wifiChanged || keyChanged)) _face->setSystemText("", 1);
    _face->setHint("บันทึกการตั้งค่าแล้ว", 1200);
}

// ============================================================
// Main loop
// ============================================================

void AgentController::loop() {
    Serial.println("[Agent] task started on core 1");
    for (;;) {
        if (gSettings.version() != _cfgVersion) {
            _cfgVersion = gSettings.version();
            applySettings(false);
        }
        // the assistant's voice reaches the speaker through this loop (_gemini.poll): a step that
        // blocks for longer than the jitter buffer makes the reply stutter - time each one
        const uint32_t loopStart = millis();
        uint32_t mark = loopStart, slowMs = 0;
        const char* slow = "";
        auto lap = [&](const char* name) {
            const uint32_t t = millis();
            if (t - mark > slowMs) slowMs = t - mark, slow = name;
            mark = t;
        };
        handleCommands();
        lap("commands");
        handleAsks();
        lap("asks");
        handleWebResults();
        lap("webResults");
        handleToolRequests();
        lap("tools");
        handleConnectivity();
        lap("connectivity");
        _gemini.poll(30);
        lap("gemini");
        handleAudioInput();
        lap("audioIn");
        deliverAnnouncements();
        lap("announce");
        // sampled after the handlers above, which stamp _lastActivity/_lastVoice with millis():
        // an older `now` made `now - _lastActivity` wrap around and put the agent to sleep
        const uint32_t now = millis();
        handleScheduler(now);
        lap("scheduler");
        handleSleepAndRestarts(now);
        lap("sleep");
        handleConsolidation(now);
        lap("consolidation");
        updateState(now);
        lap("state");
        const uint32_t took = millis() - loopStart;
        if (took > _loopMaxMs) _loopMaxMs = took;
        if (took > 150) {
            Serial.printf("[Agent] loop held %lu ms (slowest: %s %lu ms)\n", (unsigned long)took, slow,
                          (unsigned long)slowMs);
        }
        if (now >= _nextStatus) {
            _nextStatus = now + 250;
            publishStatus();
        }
        if (now >= _nextInfo) {
            _nextInfo = now + 1000;
            updateInfo();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void AgentController::handleCommands() {
    uint8_t cmd;
    while (_commands && xQueueReceive(_commands, &cmd, 0) == pdTRUE) {
        noteActivity();
        switch (cmd) {
            case CMD_TAP:
                if (_ringing) {
                    _ringing = false;
                    _face->setHint("ปิดเสียงแจ้งเตือนแล้ว", 1500);
                } else if (_sleeping) {
                    wake(false);
                } else if (_holdUntil || _fatalError) {
                    retryNow();
                } else if (_audio->isPlaying()) {
                    _audio->clearPlayback();
                    _dropAudio = true;
                    _dropSince = millis();
                    flushAgentTurn(true);
                    _face->setHint("หยุดพูดแล้ว", 1500);
                } else {
                    // the mic has its own button now: a tap on the face just makes the eyes react
                    _face->setExpression(Emotion::Happy, 1500);
                    _face->poke();
                }
                break;
            case CMD_TOGGLE_MIC:
                toggleMic();
                break;
            case CMD_NEW_SESSION:
                startFreshSession("เริ่มบทสนทนาใหม่ ความจำยังอยู่ครบ");
                break;
            case CMD_CLEAR_MEMORY:
                for (int i = 0; i < 100 && !_sink.idle(); i++) vTaskDelay(pdMS_TO_TICKS(20));
                _memory.clearAll();
                _face->setHint("ล้างความจำแล้ว", 2000);
                break;
            case CMD_CLEAR_HISTORY:
                for (int i = 0; i < 100 && !_sink.idle(); i++) vTaskDelay(pdMS_TO_TICKS(20));
                _episodes.clearAll();
                _face->setHint("ล้างประวัติแล้ว", 2000);
                break;
            case CMD_SLEEP:
                enterSleep();
                break;
            case CMD_WAKE:
                wake(false);
                break;
            case CMD_RETRY:
                if (_sleeping) wake(false);
                retryNow();
                break;
            case CMD_API_TEST:
                startApiTest();
                break;
            case CMD_KEY_QUERY:
            case CMD_KEY_HEADER:
                _gemini.setKeyInHeader(cmd == CMD_KEY_HEADER);
                _gemini.configure(_cfg.apiKey);
                _liveSel.reset();
                _resumeHandle = "";
                retryNow();
                Serial.printf("[Agent] Live API key passed in the %s\n", cmd == CMD_KEY_HEADER ? "header" : "URL");
                break;
            case CMD_RECORD:
                startRecording(_recRequestSec);
                break;
            case CMD_SEARCH_RETRY:
                if (_searchOffUntil) {
                    _searchOffUntil = 0;
                    Serial.println("[Agent] Google Search offered again - fresh session");
                    if (_gemini.started()) startFreshSession("ลองเปิด Google Search อีกครั้ง");
                }
                break;
            case CMD_POWER_SAVE:  // console "powersave": sleep now and show the clock
                if (!_sleeping) enterSleep();
                enterPowerSave();
                break;
            case CMD_RESTART:
                Serial.println("[Agent] Restarting: closing the Live session and flushing memory");
                _face->setSystemText("กำลังรีสตาร์ท...", 0);
                flushUserTurn();
                flushAgentTurn(false);
                if (_gemini.started()) _gemini.stop();  // clean close: no session left on the server
                for (int i = 0; i < 100 && !_sink.idle(); i++) vTaskDelay(pdMS_TO_TICKS(20));
                vTaskDelay(pdMS_TO_TICKS(200));
                ESP.restart();
                break;
            case CMD_SETUP_MODE:
                _setupMode = (SetupMode)(_requestedSetupMode > 2 ? 0 : _requestedSetupMode);
                _liveSel.reset();
                _resumeHandle = "";
                retryNow();
                if (_gemini.started()) _gemini.restart();
                Serial.printf("[Agent] Setup mode %u (0 full, 1 no search, 2 minimal)\n", (unsigned)_setupMode);
                break;
        }
    }
}

void AgentController::handleToolRequests() {
    ToolRequest req;
    while (_toolRequests && xQueueReceive(_toolRequests, &req, 0) == pdTRUE) {
        JsonDocument args(JsonPsramAllocator::instance());
        if (req.args && *req.args && deserializeJson(args, req.args)) {
            *req.result = "{\"error\":\"invalid JSON arguments\"}";
        } else {
            JsonDocument response(JsonPsramAllocator::instance());
            if (strcmp(req.name, "_diag") == 0) {
                fillDiagnostics(response.to<JsonObject>());  // console / PC app
            } else if (webtools::isWebTool(req.name)) {  // async: "TOOL <name> <json>" is printed when done
                const bool queued = _web.submit("console", req.name, req.args && *req.args ? req.args : "{}", kConsoleTag);
                response["status"] = queued ? "fetching - the result follows as a TOOL line and on the screen" : "busy";
            } else {
                _tools->execute(req.name, args.as<JsonObjectConst>(), response.to<JsonObject>());
            }
            req.result->clear();
            serializeJson(response, *req.result);
        }
        xSemaphoreGive(req.done);
    }
}

// ============================================================
// Connectivity, session, model fallback
// ============================================================

void AgentController::handleConnectivity() {
    const uint32_t now = millis();
    if (!_cfg.wifiSsid[0]) return;

    if (!_wifiStarted) {
        static bool eventsRegistered = false;
        if (!eventsRegistered) {  // why a connection failed (debug log / PC app)
            eventsRegistered = true;
            WiFi.onEvent(
                [](arduino_event_id_t, arduino_event_info_t info) {
                    const uint8_t r = info.wifi_sta_disconnected.reason;
                    gWifiReason = r;
                    Serial.printf("[WiFi] Disconnect reason %u\n", r);
                },
                ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        }
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);  // power save adds latency/jitter to audio streaming
        WiFi.setAutoReconnect(false);  // retried below with back-off instead of every second
        _wifiRetryMs = kWifiRetryMinMs;
        _wifiFailures = 0;
        WiFi.begin(_cfg.wifiSsid, _cfg.wifiPass);
        _wifiStarted = true;
        _wifiBeginAt = now;
        Serial.printf("[WiFi] Connecting to %s...\n", _cfg.wifiSsid);
    }
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up != _wifiUp) {
        _wifiUp = up;
        if (up) {
            Serial.printf("[WiFi] Connected, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            _wifiRetryMs = kWifiRetryMinMs;
            _wifiFailures = 0;
            if (_wifiErrorShown) {
                _wifiErrorShown = false;
                _face->setSystemText("", 1);
            }
            if (!_timeStarted) {
                configTzTime(TIME_TZ, TIME_NTP_1, TIME_NTP_2, TIME_NTP_3);
                _timeStarted = true;
            }
        } else {
            Serial.println("[WiFi] Disconnected");
            _wifiBeginAt = now;
        }
    }
    if (!up && now - _wifiBeginAt > _wifiRetryMs) {
        _wifiFailures++;
        if (_wifiFailures >= 3 && !_holdUntil && !_fatalError) {  // tell the user why
            char buf[160];
            snprintf(buf, sizeof(buf), "ต่อ Wi-Fi \"%s\" ไม่ได้: %s (รหัส %u)", _cfg.wifiSsid,
                     wifiReasonTh(gWifiReason.load()), (unsigned)gWifiReason.load());
            _face->setSystemText(buf, 0);
            _wifiErrorShown = true;
        }
        Serial.printf("[WiFi] Retry %u in %lu s (last reason %u)\n", (unsigned)_wifiFailures,
                      (unsigned long)(_wifiRetryMs / 1000), (unsigned)gWifiReason.load());
        WiFi.disconnect();
        WiFi.begin(_cfg.wifiSsid, _cfg.wifiPass);
        _wifiBeginAt = now;
        _wifiRetryMs = _wifiRetryMs * 2 > kWifiRetryMaxMs ? kWifiRetryMaxMs : _wifiRetryMs * 2;
    }

    if (!_timeValidSeen && nowEpoch()) {
        _timeValidSeen = true;
        char buf[24];
        timefmt::formatDateTime(nowEpoch(), TIME_UTC_OFFSET_SEC, buf, sizeof(buf));
        Serial.printf("[Time] Synced: %s (UTC+7)\n", buf);
        if (_storage->kind() != StorageKind::None) {
            _episodes.open(_storage->episodesDir(), &_sink, nowEpoch());
            const int keep = _storage->kind() == StorageKind::SdCard ? MEMORY_EPISODE_KEEP_DAYS_SD
                                                                     : MEMORY_EPISODE_KEEP_DAYS_FLASH;
            _episodes.prune(keep, nowEpoch());
            _tasks.purgeDone(nowEpoch(), 30);
        }
    }

    if (_holdUntil && monoSec() >= _holdUntil) {
        Serial.println("[Agent] Pause over, trying Gemini again");
        _holdUntil = 0;
        _face->setSystemText("", 1);
    }
    const bool wanted = up && _configOk && !_sleeping && !_fatalError && !_holdUntil;
    if (wanted && !_gemini.started()) _gemini.start();

    if (_reconnectPending && !_audio->isPlaying() && _gemini.started()) {
        _reconnectPending = false;
        _gemini.restart();  // resumes with the handle
    }
}

void AgentController::startFreshSession(const char* hint) {
    flushUserTurn();
    flushAgentTurn(false);
    _resumeHandle = "";
    _audio->clearPlayback();
    _dropAudio = false;
    _awaitingReply = false;
    if (hint) _face->setHint(hint, 2000);
    if (_gemini.started()) _gemini.restart();
}

void AgentController::buildSystemPrompt(PsString& p) {
    const persona::Personality& pers = persona::kPersonalities[_cfg.personality % persona::kPersonalityCount];
    p.reserve(5000);
    p = "คุณคือ \"";
    p += _cfg.agentName;
    p += "\" ผู้ช่วยส่วนตัว AI บนอุปกรณ์ตั้งโต๊ะของผู้ใช้";
    if (_cfg.userName[0]) {
        p += " ผู้ใช้ชื่อ \"";
        p += _cfg.userName;
        p += "\"";
    }
    p += "\nนิสัย: ";
    p += pers.prompt;
    p += "\nการพูด: ตอบเป็นภาษาเดียวกับที่ผู้ใช้พูด (ปกติภาษาไทย) สั้น เป็นธรรมชาติ ";
    p += persona::politeParticle((persona::Gender)_cfg.gender);
    p += " ไม่อ่านสัญลักษณ์หรือรายการยาว\n"
         "ดวงตา: คุณมีดวงตาบนจอ เรียก set_expression ให้เข้ากับอารมณ์ที่กำลังพูดอย่างเงียบๆ\n"
         "ความจำ: เรียก save_memory/save_relation ทันทีที่รู้ข้อมูลสำคัญของผู้ใช้ และ recall_memory "
         "ก่อนตอบเรื่องส่วนตัว ห้ามแต่งความจำ\n"
         "ผู้ช่วยส่วนตัว: ใช้ set_timer, set_reminder, add_task ฯลฯ ตามที่ผู้ใช้ขอ ";
    p += "ข้อมูลสด (แสดงกราฟิกบนจอให้เอง แล้วคุณพูดสรุปสั้นๆ): อากาศ/ฝน/ฝุ่น PM2.5 = get_weather, "
         "ราคาน้ำมันในไทย = get_oil_price, หุ้น/ดัชนี/ทองคำ/คริปโต/ค่าเงิน/น้ำมันดิบ = get_market_quote, "
         "แลกเงิน = convert_currency, ข่าว = get_news, คิดเลข = calculate (ห้ามคิดเลขเอง), แปลงหน่วย = convert_units, "
         "เวลาต่างประเทศ = get_world_time, ความรู้ทั่วไป/ใครคือ/คืออะไร = search_knowledge, จดโน้ตยาว = notes ";
    if (_cfg.financeTools) {
        p += "การเงิน: ภาพรวมตลาด = market_overview, วิเคราะห์เทคนิค = market_analysis, หุ้นเด่น/ร่วง = market_scan, "
             "ข่าวเศรษฐกิจ = economic_calendar, ขนาด lot = position_size, แจ้งเตือนราคา = price_alert "
             "(บอกเสมอว่าไม่ใช่คำแนะนำการลงทุน) ";
    }
    if (_cfg.mt5Url[0]) {
        p += _cfg.mt5Trade ? "MT5: ดูพอร์ต = mt5_account, ส่งคำสั่ง = mt5_trade ต้องอ่านคำสั่งทวนและได้คำยืนยันชัดเจนก่อนเสมอ "
                           : "MT5: ดูพอร์ต = mt5_account (การส่งคำสั่งเทรดปิดอยู่) ";
    }
    p += searchAvailable() ? "เรื่องอื่นที่ต้องใช้ข้อมูลล่าสุดใช้ Google Search "
                           : "ตอนนี้ Google Search ใช้ไม่ได้ เรื่องอื่นที่ต้องค้นเว็บให้บอกตรงๆ ว่ายังค้นไม่ได้ ";
    p += "เปลี่ยนเสียง/นิสัย/ชื่อด้วย configure_assistant ถ้าผู้ใช้บอกลาหรือราตรีสวัสดิ์ให้เรียก go_to_sleep\n"
         "ควบคุมเครื่อง: เสียงดัง/เบา = set_volume (change +/-), จอสว่าง/มืด = set_brightness (change +/-), "
         "ความไวไมค์ / ตั้งเวลาพัก / ตั้งเวลาโหมดนาฬิกา = configure_device, พักหน้าจอ / โหมดนาฬิกาเดี๋ยวนี้ = rest_screen, วิทยุ = play_radio / stop_radio (เปิดแล้วพูดชื่อสถานีสั้นๆ แล้วเงียบ เพลงที่เล่นอยู่ดูได้จาก get_device_status), ปิดไมค์ = mute_microphone "
         "(เปิดไมค์ด้วยเสียงไม่ได้ บอกผู้ใช้ให้แตะปุ่มไมค์บนจอ)\n"
         "ข้อความที่ขึ้นต้นด้วย [แจ้งเตือนจากระบบ] หรือ [แจ้งเตือนราคาจากระบบ] มาจากอุปกรณ์ ให้แจ้งผู้ใช้ทันทีสั้นๆ\n";
    if (gStations.count()) {  // play_radio picks from this list (number, name or genre)
        String list;
        gStations.describe(list);
        p += "สถานีวิทยุในรายการ: ";
        p += list.c_str();
        p += "\n";
    }

    const uint32_t now = nowEpoch();
    char buf[160];
    if (now) {
        const timefmt::Civil c = timefmt::toCivil(now, TIME_UTC_OFFSET_SEC);
        snprintf(buf, sizeof(buf), "\n## ตอนนี้: วัน%s %04d-%02d-%02d %02d:%02d น.\n", timefmt::thaiWeekday(c.weekday),
                 c.year, c.month, c.day, c.hour, c.minute);
        p += buf;
    }
    if (_tasks.openCount() || !_scheduler.items().empty()) {
        snprintf(buf, sizeof(buf), "งานค้าง %u รายการ, การแจ้งเตือนที่รออยู่ %u รายการ\n",
                 (unsigned)_tasks.openCount(), (unsigned)_scheduler.items().size());
        p += buf;
    }

    PsString core;
    _memory.buildCoreMemory(core, MEMORY_CORE_PROMPT_BYTES);
    p += "\n## สิ่งสำคัญที่คุณจำได้เกี่ยวกับผู้ใช้\n";
    p += core.empty() ? PsString("(ยังไม่มี ค่อยๆ ทำความรู้จักและบันทึกไว้)\n") : core;

    PsString recent;
    _episodes.buildRecentContext(recent, MEMORY_RECENT_TURNS, MEMORY_RECENT_PROMPT_BYTES, AGENT_USER_LABEL,
                                 AGENT_SELF_LABEL);
    if (!recent.empty()) {
        p += "\n## บทสนทนาล่าสุดก่อนเซสชันนี้ (บริบท ไม่ต้องทวน)\n";
        p += recent;
    }
}

void AgentController::buildSetup(JsonObject setup) {
    const uint8_t model = _liveSel.active(monoSec());
    if (model != _activeModel) {
        _resumeHandle = "";  // a handle belongs to one model's session
        _activeModel = model;
    }
    _closeSeen = false;
    String modelPath = String("models/") + liveModelId(model);
    setup["model"] = modelPath;

    JsonObject gen = setup["generationConfig"].to<JsonObject>();
    gen["responseModalities"].to<JsonArray>().add("AUDIO");
    if (_setupMode == SetupMode::Minimal) {  // diagnostic: model + modality only
        Serial.printf("[Agent] Setup: %s, MINIMAL (diagnostic)\n", liveModelId(model));
        return;
    }
    gen["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = _cfg.voice;
    if (model == (uint8_t)LiveModel::Live38Thinking) {
        gen["thinkingConfig"]["thinkingLevel"] = thinkingLevelId(_cfg.thinking);
    }

    PsString prompt;
    buildSystemPrompt(prompt);
    setup["systemInstruction"]["parts"][0]["text"] = prompt.c_str();

    _searchInSetup = searchAvailable();
    _tools->addDeclarations(setup["tools"].to<JsonArray>(), behaviorFor(model), _searchInSetup);

    setup["inputAudioTranscription"].to<JsonObject>();
    setup["outputAudioTranscription"].to<JsonObject>();
#if AGENT_SERVER_VAD_TUNING
    // Server VAD finds the start of speech inside our pre-roll; high start sensitivity catches soft
    // voices, low end sensitivity keeps Thai sentences with pauses in one turn (we send audioStreamEnd).
    _vadTuningInSetup = !_vadTuningRejected && _setupMode == SetupMode::Full;
    if (_vadTuningInSetup) {
        JsonObject vad = setup["realtimeInputConfig"]["automaticActivityDetection"].to<JsonObject>();
        vad["startOfSpeechSensitivity"] = "START_SENSITIVITY_HIGH";
        vad["endOfSpeechSensitivity"] = "END_SENSITIVITY_LOW";
        vad["prefixPaddingMs"] = 300;
        vad["silenceDurationMs"] = 900;
    }
#endif
    JsonObject cwc = setup["contextWindowCompression"].to<JsonObject>();
    cwc["triggerTokens"] = AGENT_CTX_TRIGGER_TOKENS;
    cwc["slidingWindow"]["targetTokens"] = AGENT_CTX_TARGET_TOKENS;

    JsonObject resume = setup["sessionResumption"].to<JsonObject>();
    if (_resumeHandle.length()) resume["handle"] = _resumeHandle.c_str();

    Serial.printf("[Agent] Setup: %s, %s session, voice %s, prompt %u bytes%s%s\n", liveModelId(model),
                  _resumeHandle.length() ? "resumed" : "new", _cfg.voice, (unsigned)prompt.size(),
                  _searchInSetup ? "" : ", no Google Search", model != _cfg.model ? " (FALLBACK)" : "");
}

void AgentController::onSetupComplete() {
    _failedResumes = 0;
    _sessionGen++;  // web-tool answers from an older session are dropped
    _liveSel.reportSuccess(_activeModel);
    _fatalError = false;
    if (_activeModel != _cfg.model) {
        char buf[96];
        snprintf(buf, sizeof(buf), "ใช้ %s แทนชั่วคราว", liveModelLabel(_activeModel));
        _face->setHint(buf, 3000);
    }
    if (_wakeBuffering) sendPreroll();
}

void AgentController::onDisconnected(bool wasReady) {
    flushUserTurn();
    flushAgentTurn(!wasReady);
    _awaitingReply = false;
    _vadOpen = false;
    const bool closeSeen = _closeSeen;
    _closeSeen = false;
    if (wasReady || _sleeping || !_gemini.started()) return;
    if (closeSeen) return;  // already classified from the server's close frame
    // Dropped before setupComplete: a bad resume handle or an unhealthy model
    if (_resumeHandle.length() && ++_failedResumes >= 2) {
        _resumeHandle = "";
        _failedResumes = 0;
        return;
    }
    const uint32_t sec = monoSec();
    if (_liveSel.reportFailure(_activeModel, fallback::Failure::Transient, sec)) {
        Serial.printf("[Fallback] %s keeps failing -> %s\n", liveModelId(_activeModel),
                      liveModelId(_liveSel.active(sec)));
    }
    if (_liveSel.allCoolingDown(sec)) enterHold(sec);
}

void AgentController::onServerClose(int code, const char* reason) {
    const fallback::Failure f = fallback::classifyClose(code, reason);
    Serial.printf("[Agent] Server close %d (%s): %s\n", code, fallback::failureName(f), reason ? reason : "");
    _closeSeen = true;
    if (f != fallback::Failure::None) {
        char buf[100];
        snprintf(buf, sizeof(buf), "%d %s: %s", code, fallback::failureName(f), reason ? reason : "");
        _lastClose = buf;
    }
    if (f == fallback::Failure::Auth) {
        _fatalError = true;
        _errorText = "API key ใช้ไม่ได้ ตรวจในเมนูตั้งค่า";
        _face->setSystemText(_errorText.c_str(), 0);
        if (_gemini.started()) _gemini.stop();
        return;
    }
    // A model that does not accept realtimeInputConfig answers 1007 (invalid argument) or "not
    // supported": retry the same model without the VAD tuning instead of putting it on a cool-down.
    const bool mentionsVad = reason && (strstr(reason, "realtime") || strstr(reason, "activity") ||
                                        strstr(reason, "Realtime") || strstr(reason, "sensitivity"));
    if (_vadTuningInSetup && !_gemini.isReady() && (code == 1007 || mentionsVad)) {  // only a setup rejection
        _vadTuningRejected = true;
        Serial.printf("[Fallback] %s rejected the VAD tuning -> retrying without it\n", liveModelId(_activeModel));
        _resumeHandle = "";
        _reconnectPending = true;
        return;
    }
    if (f == fallback::Failure::SessionExpired) {  // stale resume handle: fresh session, same model
        Serial.println("[Agent] Resume handle expired -> fresh session");
        _resumeHandle = "";
        _failedResumes = 0;
        _reconnectPending = true;
        return;
    }
    const uint32_t sec = monoSec();
    // Grounding with Google Search has its own quota, and when it is used up the server closes the
    // whole session with the same "exceeded your current quota" text (verified 2026-10-14: every
    // live model failed with search, all worked without). Drop search before blaming the model.
    if (f == fallback::Failure::Quota && _searchInSetup) {
        _searchOffUntil = sec + kSearchOffSec;
        Serial.printf("[Fallback] quota error with Google Search -> retrying %s without search for %lu h\n",
                      liveModelId(_activeModel), (unsigned long)(kSearchOffSec / 3600));
        _face->setHint("โควตา Google Search หมด ปิดการค้นหาชั่วคราว", 3000);
        _resumeHandle = "";
        _reconnectPending = true;
        return;
    }
    const uint8_t failed = _activeModel;
    const bool switched = _liveSel.reportFailure(failed, f, sec);
    if (_liveSel.allCoolingDown(sec)) {  // every model failed: not a model problem, stop cycling
        enterHold(sec);
        return;
    }
    if (switched) {
        const uint8_t next = _liveSel.active(sec);
        Serial.printf("[Fallback] %s unavailable (%s) -> switching to %s\n", liveModelId(failed),
                      fallback::failureName(f), liveModelId(next));
        char buf[96];
        snprintf(buf, sizeof(buf), "สลับไปใช้ %s", liveModelLabel(next));
        _face->setHint(buf, 3000);
        _resumeHandle = "";
        _reconnectPending = true;  // reconnect now with the next model
    }
}

bool AgentController::searchAvailable() const {
    return _cfg.googleSearch && _setupMode == SetupMode::Full && monoSec() >= _searchOffUntil;
}

void AgentController::enterHold(uint32_t sec) {
    uint32_t wait = _liveSel.nextRecoveryIn(sec);
    if (wait < 30) wait = 30;
    _holdUntil = sec + wait;
    _reconnectPending = false;
    _resumeHandle = "";
    if (_gemini.started()) _gemini.stop();

    const fallback::Failure f = _liveSel.lastFailure();
    const char* what = "เชื่อมต่อ Gemini ไม่สำเร็จ";
    if (f == fallback::Failure::Quota) what = "โควตา API key หมดทุกโมเดล";
    else if (f == fallback::Failure::NotFound) what = "API key นี้ใช้โมเดล Live ไม่ได้";
    else if (f == fallback::Failure::Unavailable) what = "Gemini ไม่ว่างทุกโมเดล";
    char buf[160];
    const uint32_t epoch = nowEpoch();
    if (epoch) {
        const timefmt::Civil c = timefmt::toCivil(epoch + wait, TIME_UTC_OFFSET_SEC);
        snprintf(buf, sizeof(buf), "%s • ลองใหม่ %02d:%02d หรือแตะจอ", what, c.hour, c.minute);
    } else {
        snprintf(buf, sizeof(buf), "%s • ลองใหม่ใน %lu นาที หรือแตะจอ", what, (unsigned long)((wait + 59) / 60));
    }
    _errorText = buf;
    _face->setSystemText(buf, 0);
    Serial.printf("[Fallback] all live models cooling down (%s) -> pausing %lu s\n", fallback::failureName(f),
                  (unsigned long)wait);
}

void AgentController::startApiTest() {
    // Text models first (the summary uses generate[0]). The live models are called with
    // generateContent too: they normally refuse it, but a quota problem comes back as HTTP 429
    // with the full QuotaFailure (the WebSocket close reason is cut at 123 bytes).
    static const char* const kModels[] = {"gemini-3.5-flash-lite", "gemini-3.1-flash-lite", "gemini-3.8-live",
                                          "gemini-3.8-live-extended-thinking", "gemini-3.1-flash-live-preview"};
    if (!_wifiUp || !_configOk) {
        _face->setHint("ต่อ Wi-Fi และใส่ API key ก่อน", 2500);
        Serial.println("APITEST {\"error\":\"wifi or api key missing\"}");
        return;
    }
    if (!_text.submitProbe(kModels, 5, kModels, 5)) {
        _face->setHint("ระบบกำลังทำงานอื่น ลองใหม่อีกครั้ง", 2500);
        Serial.println("APITEST {\"error\":\"busy\"}");
        return;
    }
    _face->setSystemText("กำลังทดสอบ API key ...", 30000);
    Serial.println("[Agent] API key self-test started");
}

void AgentController::handleProbeResult(int status, const PsString* report) {
    if (status != 200 || !report || report->empty()) {
        _face->setSystemText("ทดสอบ API key ไม่สำเร็จ (Wi-Fi?)", 8000);
        Serial.println("APITEST {\"error\":\"request failed\"}");
        return;
    }
    {  // one write so other tasks' log lines cannot split the JSON
        PsString line("APITEST ");
        line += *report;
        line += '\n';
        Serial.write((const uint8_t*)line.data(), line.size());
    }

    JsonDocument doc(JsonPsramAllocator::instance());
    if (deserializeJson(doc, report->c_str())) return;
    int seen = 0, total = 0, keyError = 0;
    for (JsonObjectConst m : doc["models"].as<JsonArrayConst>()) {
        const int h = m["http"] | 0;
        total++;
        if (h == 200) seen++;
        if (h == 400 || h == 401 || h == 403) keyError = h;
    }
    char buf[200];
    JsonObjectConst g = doc["generate"][0];
    const int gh = g["http"] | 0;
    JsonObjectConst liveQuota;  // first live model that reports a quota failure
    for (JsonObjectConst x : doc["generate"].as<JsonArrayConst>()) {
        if ((x["http"] | 0) == 429 && strstr(x["id"] | "", "live")) {
            liveQuota = x;
            break;
        }
    }
    if (keyError) {
        snprintf(buf, sizeof(buf), "API key ใช้ไม่ได้ (HTTP %d) ตรวจ key ในเมนูตั้งค่า", keyError);
    } else if (gh == 200 && !liveQuota.isNull()) {
        snprintf(buf, sizeof(buf), "key ใช้ได้ (Flash-Lite OK) แต่ Live API โควตาหมด (limit %s) ตรวจ billing/limit ใน AI Studio",
                 liveQuota["error"]["quota"][0]["limit"] | "?");
    } else if (gh == 200) {
        snprintf(buf, sizeof(buf), "API key ใช้ได้ • เห็นโมเดล %d/%d • เรียก Flash-Lite ได้", seen, total);
    } else if (gh == 429) {
        const char* limit = g["error"]["quota"][0]["limit"] | "?";
        const char* id = g["error"]["quota"][0]["id"] | "";
        snprintf(buf, sizeof(buf), "API key ถูกต้อง แต่โควตาหมด (limit %s%s) ตรวจ billing ใน AI Studio", limit,
                 strstr(id, "FreeTier") ? ", free tier" : "");
    } else {
        snprintf(buf, sizeof(buf), "API key: เห็นโมเดล %d/%d • Flash-Lite HTTP %d", seen, total, gh);
    }
    _lastProbe = buf;
    _face->setSystemText(buf, 20000);
}

void AgentController::retryNow() {
    Serial.println("[Agent] Retry requested");
    _holdUntil = 0;
    _fatalError = false;
    _errorText = "";
    _face->setSystemText("", 1);
    _face->setHint("กำลังลองเชื่อมต่อใหม่", 2000);
}

void AgentController::toggleMic() {
    _muted = !_muted;
    const bool muted = _muted;
    gSettings.update([muted](SettingsData& d) { d.micMuted = muted; });  // remembered across reboots
    _face->setHint(_muted ? "ปิดไมค์แล้ว" : "เปิดไมค์แล้ว", 1500);
    _audio->playTone(_muted ? 440 : 880, 80, 3000);
    Serial.printf("[Agent] Mic %s\n", _muted ? "off" : "on");
}

void AgentController::onAudio(const int16_t* pcm, size_t samples) {
    if (_dropAudio) return;
    if (!_userTurn.empty()) flushUserTurn();
    _awaitingReply = false;
    _ringing = false;
    _lastAgentAudio = millis();
    markAnswered();
    noteActivity();
    _audio->writePlayback((const uint8_t*)pcm, samples * 2);
}

void AgentController::onInputTranscript(const char* text) {
    if (!_agentTurn.empty()) flushAgentTurn(false);
    if (_userTurn.empty()) _userTurnTs = nowEpoch();
    _userTurn += text;
    _lastUserSpeech = millis();
    _awaitingReply = true;
    _heardSinceSend = true;
    noteActivity();
    _face->pushTranscript('u', text);
}

// 3.1 Flash Live sometimes leaks tool-call scaffolding into the output transcription
// ("functionreponsetextfunctionสวัสดีค่ะ..." after parallel tool calls): drop any run of ASCII
// letters that contains "function".
static void stripToolScaffolding(const char* in, String& out) {
    out = "";
    const size_t n = strlen(in);
    size_t i = 0;
    while (i < n) {
        if (isalpha((unsigned char)in[i])) {
            size_t j = i;
            while (j < n && isalpha((unsigned char)in[j])) j++;
            const String word = String(in + i).substring(0, j - i);
            if (word.indexOf("function") < 0) out += word;
            i = j;
        } else {
            out += in[i++];
        }
    }
}

void AgentController::onOutputTranscript(const char* raw) {
    if (_dropAudio) return;
    String clean;
    stripToolScaffolding(raw, clean);
    if (clean.isEmpty()) return;
    const char* text = clean.c_str();
    if (!_userTurn.empty()) flushUserTurn();
    if (_agentTurn.empty()) _agentTurnTs = nowEpoch();
    _agentTurn += text;
    _awaitingReply = false;
    markAnswered();
    _face->pushTranscript('a', text);
}

void AgentController::onInterrupted() {
    _audio->clearPlayback();
    flushAgentTurn(true);
    _dropAudio = false;
}

void AgentController::onGenerationComplete() { _audio->flushPlayback(); }

void AgentController::onTurnComplete() {
    Serial.printf("[Chat] turn complete (tokens/min %lu)\n", (unsigned long)tokensPerMinute());
    flushUserTurn();
    flushAgentTurn(_dropAudio);
    _audio->flushPlayback();
    _dropAudio = false;
    _awaitingReply = false;
}

void AgentController::onToolCall(JsonArrayConst calls) {
    noteActivity();
    markAnswered();
    JsonDocument resp(JsonPsramAllocator::instance());
    JsonArray responses = resp["toolResponse"]["functionResponses"].to<JsonArray>();
    for (JsonObjectConst call : calls) {
        const char* name = call["name"] | "";
        if (webtools::isWebTool(name)) {  // HTTPS in the webTools task; answered later with the same id
            String args;
            serializeJson(call["args"], args);
            if (_web.submit(call["id"] | "", name, args.c_str(), _sessionGen)) {
                Serial.printf("[Tool] %s %s -> fetching...\n", name, args.c_str());
                _face->setHint("กำลังดึงข้อมูล...", 2500);
                _toolBusyUntil = millis() + 15000;
                continue;
            }
        }
        JsonObject fr = responses.add<JsonObject>();
        if (!call["id"].isNull()) fr["id"] = call["id"];
        fr["name"] = name;
        JsonObject response = fr["response"].to<JsonObject>();
        const bool ok = _tools->execute(name, call["args"].as<JsonObjectConst>(), response);
        // On gemini-3.8-live set_expression is NON_BLOCKING: answer silently
        if (_activeModel == (uint8_t)LiveModel::Live38 && AgentTools::isSilentTool(name)) {
            response["scheduling"] = "SILENT";
        }
        if (strcmp(name, "set_expression") != 0) {
            Serial.printf("[Tool] %s -> %s\n", name, ok ? "ok" : (response["error"] | "error"));
        }
        if (!ok) continue;
        if (!strcmp(name, "save_memory") || !strcmp(name, "save_relation")) {
            _face->setHint("จำไว้แล้ว", 1500);
            _face->setExpression(Emotion::Happy, 1500);
            _turnsSinceConsolidation = 0;  // the live model is already saving
        } else if (!strcmp(name, "recall_memory") || !strcmp(name, "recall_conversation")) {
            _face->setHint("กำลังนึก...", 1200);
        } else if (!strcmp(name, "set_timer") || !strcmp(name, "set_reminder")) {
            _face->setHint("ตั้งแจ้งเตือนแล้ว", 1500);
        } else if (!strcmp(name, "add_task")) {
            _face->setHint("เพิ่มงานแล้ว", 1500);
        }
    }
    if (calls.size() == 1 && !strcmp(calls[0]["name"] | "", "set_expression")) {
        // purely cosmetic: does not count as "thinking"
    } else if ((int32_t)(_toolBusyUntil - millis()) < 600) {
        _toolBusyUntil = millis() + 600;
    }
    if (responses.size() && !_gemini.sendJson(resp)) Serial.println("[Tool] ERROR: failed to send toolResponse");
}

// Finished web-tool calls: answer the model (same session only) or print for the console / PC app
void AgentController::handleWebResults() {
    webtools::Result r;
    while (_web.poll(r)) {
        JsonDocument result(JsonPsramAllocator::instance());
        deserializeJson(result, r.json->c_str());
        if (r.tag == webtools::kAlertTag) {  // a price alert fired in the background
            const char* m = result["message"] | "";
            Serial.printf("[Alert] %s\n", m);
            PsString msg = "[แจ้งเตือนราคาจากระบบ] ";
            msg += m;
            msg += " — บอกผู้ใช้สั้นๆ";
            _announcements.push_back(msg);
            _face->setSystemText(m, 15000);
            _face->setExpression(Emotion::Surprised, 4000);
            _ringing = true;
            _ringUntil = millis() + 10000;
            _nextChime = millis();
            noteActivity();
            if (_sleeping) wake(false);
            delete r.json;
            continue;
        }
        const bool ok = result["error"].isNull();
        Serial.printf("[Tool] %s -> %s (%.1f s)\n", r.name, ok ? "ok" : (const char*)(result["error"] | "error"),
                      r.elapsedMs / 1000.0f);
        if (r.tag == kConsoleTag) {
            PsString line("TOOL ");
            line += r.name;
            line += ' ';
            line += *r.json;
            line += '\n';
            Serial.write((const uint8_t*)line.data(), line.size());
        } else if (r.tag == _sessionGen && _gemini.isReady()) {
            JsonDocument resp(JsonPsramAllocator::instance());
            JsonObject fr = resp["toolResponse"]["functionResponses"].add<JsonObject>();
            if (r.id[0]) fr["id"] = r.id;
            fr["name"] = r.name;
            fr["response"] = result.as<JsonObjectConst>();
            if (!_gemini.sendJson(resp)) Serial.println("[Tool] ERROR: failed to send toolResponse");
        } else {
            Serial.printf("[Tool] %s result dropped (session changed)\n", r.name);
        }
        if (!ok) _face->setHint("ดึงข้อมูลไม่สำเร็จ", 2000);
        _toolBusyUntil = millis() + 600;
        delete r.json;
    }
}

void AgentController::onToolCallCancellation(JsonArrayConst ids) {
    Serial.printf("[Tool] %u call(s) cancelled by the server\n", (unsigned)ids.size());
}

void AgentController::onSessionHandle(const char* handle, bool resumable) {
    if (resumable && handle && *handle) _resumeHandle = handle;
}

void AgentController::onGoAway(uint32_t msLeft) {
    Serial.printf("[Agent] goAway, %lu ms left - will resume\n", (unsigned long)msLeft);
    _reconnectPending = true;
}

void AgentController::onUsage(uint32_t totalTokens) {
    const uint32_t now = millis();
    const uint32_t slot = (now / 10000) % 6;
    if (now / 10000 != _tokBucketAt) {
        // clear the buckets we skipped
        const uint32_t steps = now / 10000 - _tokBucketAt;
        for (uint32_t i = 1; i <= steps && i <= 6; i++) _tokBucket[(_tokBucketAt + i) % 6] = 0;
        _tokBucketAt = now / 10000;
    }
    _tokBucket[slot] += totalTokens;
}

uint32_t AgentController::tokensPerMinute() const {
    if (millis() / 10000 - _tokBucketAt >= 6) return 0;
    uint32_t sum = 0;
    for (uint32_t v : _tokBucket) sum += v;
    return sum;
}

// ============================================================
// Microphone: half-duplex gate, local VAD, wake detection
// ============================================================

// Quiet speech (user far from the board) is lifted towards AGENT_AGC_TARGET: fast to reduce
// (no clipping), slow to raise. Updated on voiced chunks only so silence is not pumped up.
void AgentController::updateAgc(float level) {
    if (level < 1e-4f) return;
    float want = AGENT_AGC_TARGET / level;
    if (want < 1.0f) want = 1.0f;
    if (want > AGENT_AGC_MAX_GAIN) want = AGENT_AGC_MAX_GAIN;
    _agcGain += (want - _agcGain) * (want < _agcGain ? 0.3f : 0.06f);
}

void AgentController::sendMic(const int16_t* pcm, size_t n) {
    static int16_t out[kChunk];
    if (n > kChunk) n = kChunk;
    for (size_t i = 0; i < n; i++) {
        float v = pcm[i] * _agcGain;
        out[i] = (int16_t)(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : v));
    }
    _gemini.sendAudio(out, n, AUDIO_UPLOAD_RATE);
    _uttChunks++;
}

void AgentController::sendPreroll() {
    static int16_t chunk[kChunk];
    size_t n;
    while ((n = _preroll.pop(chunk, kChunk)) > 0) sendMic(chunk, n);
    _wakeBuffering = false;
    _vadOpen = true;
    _lastVoice = millis();
}

void AgentController::beginUtterance(uint32_t now, float level, bool followUp) {
    _uttStart = now;
    _uttChunks = 0;
    _uttVoiced = 0;
    _uttPeak = level;
    _uttFollowUp = followUp;
    if (followUp) {
        Serial.printf("[Mic] ฟังต่อ %u วิ หลัง AI ตอบ (ส่งเสียงต่อเนื่อง)\n", (unsigned)(AGENT_FOLLOWUP_MS / 1000));
    } else {
        Serial.printf("[Mic] เริ่มส่งเสียงให้ AI (ระดับ %.3f, เกณฑ์ %.3f, ขยาย x%.1f)\n", level, _threshold, _agcGain);
    }
}

void AgentController::endUtterance(uint32_t now) {
    if (!_vadOpen) return;
    _vadOpen = false;
    _onset = 0;
    _gemini.sendAudioStreamEnd();
    const float sentSec = _uttChunks * (AUDIO_MIC_CHUNK_MS / 1000.0f);
    const float voicedSec = _uttVoiced * (AUDIO_MIC_CHUNK_MS / 1000.0f);
    if (_uttVoiced >= 3) {  // real speech: from now on expect an answer
        Serial.printf("[Mic] จบประโยค: ส่ง %.1f วิ (เสียงพูด %.1f วิ, ดังสุด %.3f, ขยาย x%.1f) -> รอ AI ตอบ\n", sentSec,
                      voicedSec, _uttPeak, _agcGain);
        _replyWatchAt = now;
        _heardSinceSend = false;
    } else if (_uttFollowUp) {
        Serial.println("[Mic] หมดช่วงฟังต่อ (ไม่มีเสียงพูด)");
    } else {
        Serial.printf("[Mic] เสียงสั้นเกินไป %.1f วิ (ไม่นับเป็นคำพูด)\n", voicedSec);
    }
}

// Voice that never reached the onset: the mic icon flashed but nothing was sent
void AgentController::noteBlip(uint32_t now, bool voice, float level) {
    if (voice) {
        _blipChunks++;
        if (level > _blipPeak) _blipPeak = level;
        return;
    }
    if (_blipChunks && sinceMs(now, _lastBlipLog) > 3000) {
        _lastBlipLog = now;
        Serial.printf("[Mic] ได้ยินเสียงสั้นๆ %u ms (ดังสุด %.3f / เกณฑ์ %.3f) ไม่ถึงเกณฑ์ - ไม่ได้ส่ง\n",
                      (unsigned)(_blipChunks * AUDIO_MIC_CHUNK_MS), _blipPeak, _threshold);
    }
    _blipChunks = 0;
    _blipPeak = 0;
}

void AgentController::checkNoReply(uint32_t now) {
    if (!_replyWatchAt || sinceMs(now, _replyWatchAt) < AGENT_NO_REPLY_WARN_MS) return;
    _replyWatchAt = 0;
    if (_heardSinceSend) {
        Serial.printf("[Mic] ⚠ AI ได้ยินแล้ว (มี transcript) แต่ไม่ตอบใน %u วิ - %s\n",
                      (unsigned)(AGENT_NO_REPLY_WARN_MS / 1000),
                      _activeModel == (uint8_t)LiveModel::Live38 ? "3.8 Live อาจเลือกไม่ตอบ (proactive audio)" : "ลองพูดใหม่");
    } else {
        Serial.printf("[Mic] ⚠ ส่งเสียงพูดแล้วแต่ AI ไม่ได้ยิน/ไม่ตอบใน %u วิ (ไม่มี transcript) - พูดใกล้ขึ้นหรือดังขึ้น\n",
                      (unsigned)(AGENT_NO_REPLY_WARN_MS / 1000));
    }
    _face->setHint("AI ไม่ตอบ ลองพูดอีกครั้ง", 2500);
}

void AgentController::handleAudioInput() {
    const uint32_t now = millis();
    const bool speaking = _audio->isPlaying() || _audio->msSincePlayback() < AGENT_ECHO_TAIL_MS;
    const bool allow = _configOk && !_muted && !(AGENT_HALF_DUPLEX && speaking);
    if (allow != _gateAllowed) {
        _audio->setCaptureEnabled(allow);
        _gateAllowed = allow;
        if (!allow) {
            endUtterance(now);
            _onset = 0;
            if (!_wakeBuffering) _preroll.clear();
        } else if (sinceMs(now, _lastAgentAudio) < 3000 && _gemini.isReady() && !_sleeping) {
            _followUpUntil = now + AGENT_FOLLOWUP_MS;  // the assistant just finished: users answer right away
        }
    }
    checkNoReply(now);
    if (_recActive && (int32_t)(now - _recDeadline) >= 0) finishRecording();

    static int16_t chunk[kChunk];
    for (int iter = 0; iter < 6 && _audio->captureAvailable() >= sizeof(chunk); iter++) {
        _audio->readCapture((uint8_t*)chunk, sizeof(chunk));
        const float level = rms(chunk, kChunk);
        _threshold = _noise * AGENT_VAD_NOISE_FACTOR;
        if (_threshold < AGENT_VAD_MIN_LEVEL) _threshold = AGENT_VAD_MIN_LEVEL;
        const bool voice = level > _threshold;
        if (voice) {
            _lastVoice = now;
            noteActivity();
        } else {
            _noise += (level - _noise) * 0.05f;  // follow the background noise
        }
        bool sent = false;

        if (_sleeping) {  // only listen for a wake-up
            _preroll.push(chunk, kChunk);
            _preroll.trimTo(kWakeBufferSamples * 3 / 4);
            // Radio on: the speaker leaks into the mic. The board knows what it plays, so it learns
            // how much of the radio reaches the mic (coupling = mic level / radio level on music-only
            // chunks) and wakes only on sound clearly above that expected leak, held twice as long.
            // A tap always works.
            const bool radioOn = _audio->radioSounding();
            bool loud = voice;
            if (radioOn) {
                const float ref = _audio->radioOutLevel();
                _radioRef += (ref - _radioRef) * 0.3f;  // smooth: the acoustic path lags a few chunks
                const bool settling = (int32_t)(now - _radioSettleUntil) < 0;
                const float expected = _coupling * _radioRef;
                loud = !settling && _coupling > 0 && level > _threshold && level > expected * RADIO_WAKE_FACTOR;
                if (!loud && _radioRef > 0.003f) {
                    const float ratio = level / _radioRef;
                    _coupling = _coupling <= 0 ? ratio : _coupling + (ratio - _coupling) * (settling ? 0.2f : 0.03f);
                }
            }
            _wakeVotes = loud ? _wakeVotes + 1 : 0;
            if (_wakeVotes >= (radioOn ? AGENT_WAKE_CHUNKS * 2 : AGENT_WAKE_CHUNKS)) wake(true);
            recordChunk(chunk, kChunk, voice, false);
            continue;
        }
        if (!_gemini.isReady()) {  // connecting: keep what the user is saying
            _preroll.push(chunk, kChunk);
            _preroll.trimTo(_wakeBuffering ? kWakeBufferSamples : kPrerollSamples);
            recordChunk(chunk, kChunk, voice, false);
            continue;
        }
        if (_wakeBuffering) {
            beginUtterance(now, level, false);
            sendPreroll();
        }

#if AGENT_LOCAL_VAD
        if (!_vadOpen) {
            _preroll.push(chunk, kChunk);
            _preroll.trimTo(kPrerollSamples);
            _onset = voice ? _onset + 1 : 0;
            const bool followUp = (int32_t)(_followUpUntil - now) > 0;
            if (_onset >= AGENT_VAD_ONSET_CHUNKS || followUp) {
                beginUtterance(now, level, followUp && _onset < AGENT_VAD_ONSET_CHUNKS);
                if (voice) {
                    _uttVoiced = _onset;
                    updateAgc(level);
                }
                _blipChunks = 0;
                sendPreroll();  // includes this chunk
                sent = true;
            } else {
                noteBlip(now, voice, level);
            }
        } else {
            if (voice) {
                _uttVoiced++;
                if (level > _uttPeak) _uttPeak = level;
                updateAgc(level);
                _replyWatchAt = 0;  // still talking
            }
            sendMic(chunk, kChunk);
            sent = true;
            const bool followUp = (int32_t)(_followUpUntil - now) > 0;
            const bool silentFollowUp = _uttFollowUp && _uttVoiced == 0;
            if (silentFollowUp ? !followUp : sinceMs(now, _lastVoice) > AGENT_VAD_HANGOVER_MS) endUtterance(now);
            if (voice) _followUpUntil = 0;  // the user is speaking: the normal hang-over decides the end
        }
#else
        sendMic(chunk, kChunk);
        sent = true;
#endif
        recordChunk(chunk, kChunk, voice, sent);
    }
}

// ---- Diagnostic recording: exactly what the AGC would upload, 16 kHz mono, with VAD flags ----

void AgentController::startRecording(uint8_t sec) {
    if (sec < 1) sec = 1;
    if (sec > AGENT_MIC_RECORD_MAX_S) sec = AGENT_MIC_RECORD_MAX_S;
    _rec.assign((size_t)sec * AUDIO_UPLOAD_RATE, 0);
    _recFlags.clear();
    _recLen = 0;
    _recActive = true;
    _recDeadline = millis() + sec * 1000u + 3000u;
    Serial.printf("[Mic] อัดเสียง %u วิ (สิ่งที่ส่งให้ AI หลังขยายเสียง)%s\n", (unsigned)sec,
                  _muted ? " - ไมค์ปิดอยู่ จะไม่ได้เสียง" : "");
}

void AgentController::recordChunk(const int16_t* pcm, size_t n, bool voice, bool sent) {
    if (!_recActive) return;
    const size_t room = _rec.size() - _recLen;
    const size_t take = n < room ? n : room;
    for (size_t i = 0; i < take; i++) {
        float v = pcm[i] * _agcGain;
        _rec[_recLen + i] = (int16_t)(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : v));
    }
    _recLen += take;
    _recFlags.push_back((uint8_t)((voice ? 1 : 0) | (sent ? 2 : 0)));
    if (_recLen >= _rec.size()) finishRecording();
}

void AgentController::finishRecording() {
    if (!_recActive) return;
    _recActive = false;
    String flags;
    flags.reserve(_recFlags.size() + 1);
    for (uint8_t f : _recFlags) flags += (char)('0' + f);
    String info;
    info.reserve(flags.length() + 200);
    info = "RECINFO {\"rate\":";
    info += AUDIO_UPLOAD_RATE;
    info += ",\"samples\":";
    info += (unsigned)_recLen;
    info += ",\"chunkMs\":";
    info += AUDIO_MIC_CHUNK_MS;
    info += ",\"gain\":";
    info += String(_agcGain, 2);
    info += ",\"threshold\":";
    info += String(_threshold, 4);
    info += ",\"noise\":";
    info += String(_noise, 4);
    info += ",\"muted\":";
    info += _muted ? "true" : "false";
    info += ",\"flags\":\"";
    info += flags;
    info += "\"}\n";
    Serial.write((const uint8_t*)info.c_str(), info.length());
    Serial.printf("RECORD %u %u\n", (unsigned)AUDIO_UPLOAD_RATE, (unsigned)(_recLen * 2));
    if (_recLen) Serial.write((const uint8_t*)_rec.data(), _recLen * 2);  // one write: nothing interleaves
    Serial.print("\nEND\n");
    _rec.clear();
    _rec.shrink_to_fit();
    _recFlags.clear();
}

// ============================================================
// Sleep / wake, deferred restarts
// ============================================================

void AgentController::enterSleep() {
    if (_sleeping) return;
    flushUserTurn();
    flushAgentTurn(false);
    _sleeping = true;
    _sleepPending = false;
    _vadOpen = false;
    _wakeVotes = 0;
    _preroll.clear();
    if (_gemini.started()) _gemini.stop();  // no session = no tokens
    _sleepSince = millis();
    _radioSettleUntil = _sleepSince + 2500;  // radio ramps back to full: relearn its echo before listening
    _face->setHint("พักแล้ว แตะหรือพูดเพื่อปลุก", 2500);
    Serial.println("[Agent] Sleeping");
    if (_powerSaveAfterSleep) {
        _powerSaveAfterSleep = false;
        enterPowerSave();
    }
}

// Asleep for clockMinutes: clock face + dimmed backlight. Voice (local VAD) or a tap wakes it.
void AgentController::enterPowerSave() {
    if (_powerSave || !_sleeping) return;
    _powerSave = true;
    _face->setPowerSave(true);
    gUi.setBrightness(_cfg.dimBrightness);
    Serial.printf("[Agent] Power save: clock face, screen %u%%\n", (unsigned)_cfg.dimBrightness);
}

void AgentController::leavePowerSave() {
    if (!_powerSave) return;
    _powerSave = false;
    _face->setPowerSave(false);
    gUi.setBrightness(_cfg.brightness);
    Serial.println("[Agent] Power save off");
}

void AgentController::wake(bool byVoice) {
    if (!_sleeping) return;
    leavePowerSave();
    _sleeping = false;
    _wakeBuffering = byVoice;  // replay what was said once the session is ready
    if (!byVoice) _preroll.clear();
    noteActivity();
    _face->setHint("ตื่นแล้ว", 1200);
    Serial.printf("[Agent] Waking up (%s)\n", byVoice ? "voice" : "touch");
}

void AgentController::handleSleepAndRestarts(uint32_t now) {
    const bool busy = _audio->isPlaying() || sinceMs(now, _lastAgentAudio) < 600 || _awaitingReply;

    handleRadio(now, busy);
    if (_sleepPending && !busy) enterSleep();
    if (_mutePending && !busy) {  // voice can only turn the mic OFF; the screen button turns it on
        _mutePending = false;
        if (!_muted) toggleMic();
        _face->setHint("ปิดไมค์แล้ว แตะปุ่มไมค์บนจอเพื่อเปิด", 4000);
    }

    if (_restartPending && !busy && (int32_t)(now - _restartAt) >= 0) {
        _restartPending = false;
        if (_gemini.started()) startFreshSession("ใช้การตั้งค่าใหม่แล้ว");
    }

    if (_dropAudio && sinceMs(now, _dropSince) > 20000) _dropAudio = false;

    if (_cfg.sleepMinutes && !_sleeping && _gemini.started() && !busy && !_ringing && !_vadOpen &&
        sinceMs(now, _lastActivity) > (uint32_t)_cfg.sleepMinutes * 60000u) {
        enterSleep();
    }
    if (_sleeping && !_powerSave && _cfg.clockMinutes &&
        sinceMs(now, _sleepSince) > (uint32_t)_cfg.clockMinutes * 60000u) {
        enterPowerSave();
    }

    if (_ringing) {
        if (now > _ringUntil) {
            _ringing = false;
        } else if (now >= _nextChime && !_audio->isPlaying()) {
            _audio->playTone(1320, 110, 5000);
            _audio->playTone(990, 160, 5000);
            _nextChime = now + 2500;
        }
    }
}

// ============================================================
// Reminders
// ============================================================

void AgentController::handleScheduler(uint32_t now) {
    if (now < _nextSchedulerCheck) return;
    _nextSchedulerCheck = now + 1000;
    const uint32_t epoch = nowEpoch();
    if (!epoch) return;
    std::vector<sched::Reminder> fired;
    if (!_scheduler.collectDue(epoch, fired)) return;
    for (const sched::Reminder& r : fired) {
        Serial.printf("[Reminder] %s: %s\n", r.timer ? "timer" : "reminder", r.label.c_str());
        PsString msg = "[แจ้งเตือนจากระบบ] ";
        msg += r.timer ? "ตัวจับเวลาครบแล้ว: " : "ถึงเวลา: ";
        msg += r.label;
        msg += " — บอกผู้ใช้สั้นๆ";
        _announcements.push_back(msg);
        PsString shown = "แจ้งเตือน: ";
        shown += r.label;
        _face->setSystemText(shown.c_str(), 15000);
        _face->setExpression(Emotion::Surprised, 4000);
    }
    _ringing = true;
    _ringUntil = now + 30000;
    _nextChime = now;
    noteActivity();
    if (_sleeping) wake(false);
}

void AgentController::deliverAnnouncements() {
    if (_announcements.empty() || !_gemini.isReady() || _audio->isPlaying() || _vadOpen || _awaitingReply) return;
    if (_gemini.sendClientText(_announcements.front().c_str(), true)) {
        Serial.printf("[Chat] sent text turn (%u bytes)\n", (unsigned)_announcements.front().size());
        _announcements.erase(_announcements.begin());
        _awaitingReply = true;
        _lastUserSpeech = millis();
    }
}

// ============================================================
// Background memory consolidation (Flash-Lite, with fallback)
// ============================================================

void AgentController::handleConsolidation(uint32_t now) {
    TextModelClient::Result r{};
    const bool got = _text.poll(r);
    if (got && r.tag == TextModelClient::kProbeTag) {
        handleProbeResult(r.httpStatus, r.text);
        delete r.text;
    } else if (got) {
        const uint32_t epoch = nowEpoch();
        if (r.httpStatus == 200 && r.text) {
            std::vector<consolidate::Fact> facts;
            std::vector<consolidate::Relation> rels;
            size_t added = 0;
            if (consolidate::parseResult(r.text->c_str(), r.text->size(), facts, rels)) {
                for (const auto& f : facts) {
                    bool merged = false;
                    if (_memory.addFact(f.content.c_str(), f.category, f.importance, epoch, &merged) && !merged) added++;
                }
                for (const auto& x : rels) {
                    bool merged = false;
                    if (_memory.addRelation(x.subject.c_str(), x.relation.c_str(), x.object.c_str(),
                                            ltm::Category::Person, 3, epoch, &merged) && !merged) {
                        added++;
                    }
                }
            }
            _textSel.reportSuccess(_consolidateModel);
            _consolidatedUntil = _pendingUntil;
            _turnsSinceConsolidation = 0;
            Serial.printf("[Consolidate] %s: %u facts, %u relations, %u new\n", textModelId(_consolidateModel),
                          (unsigned)facts.size(), (unsigned)rels.size(), (unsigned)added);
            if (added) {
                char buf[64];
                snprintf(buf, sizeof(buf), "จัดระเบียบความจำแล้ว +%u", (unsigned)added);
                _face->setHint(buf, 2000);
            }
        } else {
            const fallback::Failure f = fallback::classifyHttp(r.httpStatus, "");
            const bool switched = _textSel.reportFailure(_consolidateModel, f, monoSec());
            Serial.printf("[Consolidate] %s failed (HTTP %d, %s)%s\n", textModelId(_consolidateModel), r.httpStatus,
                          fallback::failureName(f), switched ? " -> fallback model next time" : "");
            if (epoch) _consolidateLimit.backoff(epoch, switched ? 30 : (f == fallback::Failure::Quota ? 900 : 180));
        }
        delete r.text;
    }

    if (!_cfg.consolidate || !_configOk || !_wifiUp || _text.busy() || _sleeping) return;
    if (_turnsSinceConsolidation < CONSOLIDATE_MIN_TURNS) return;
    if (sinceMs(now, _lastActivity) < CONSOLIDATE_IDLE_MS || _audio->isPlaying()) return;
    const uint32_t epoch = nowEpoch();
    if (!_consolidateLimit.allowed(epoch)) return;

    std::vector<ltm::Turn> recent, fresh;
    _episodes.recent(24, recent);
    uint32_t newest = _consolidatedUntil;
    for (const ltm::Turn& t : recent) {
        if (t.ts > _consolidatedUntil || t.ts == 0) {
            fresh.push_back(t);
            if (t.ts > newest) newest = t.ts;
        }
    }
    PsString core, user;
    _memory.buildCoreMemory(core, 1500);
    if (!consolidate::buildUserPrompt(core, fresh, 2, 6000, user)) {
        _turnsSinceConsolidation = 0;
        return;
    }

    JsonDocument doc(JsonPsramAllocator::instance());
    doc["systemInstruction"]["parts"][0]["text"] = consolidate::systemPrompt();
    JsonObject content = doc["contents"].to<JsonArray>().add<JsonObject>();
    content["role"] = "user";
    content["parts"][0]["text"] = user.c_str();
    JsonObject gen = doc["generationConfig"].to<JsonObject>();
    gen["responseMimeType"] = "application/json";
    consolidate::buildSchema(gen["responseSchema"].to<JsonObject>());
    gen["temperature"] = 0.2;
    gen["maxOutputTokens"] = 2048;

    PsString* body = new PsString();
    body->resize(measureJson(doc) + 1);
    body->resize(serializeJson(doc, &(*body)[0], body->size()));

    _consolidateModel = _textSel.active(monoSec());
    if (_text.submit(1, textModelId(_consolidateModel), body)) {
        _consolidateLimit.record(epoch);
        _pendingUntil = newest;
        Serial.printf("[Consolidate] %u turns -> %s\n", (unsigned)fresh.size(), textModelId(_consolidateModel));
    }
}

// ============================================================
// Turns -> episodic memory
// ============================================================

void AgentController::flushUserTurn() {
    if (_userTurn.empty()) return;
    Serial.printf("[Chat] user: %s\n", _userTurn.c_str());
    _episodes.append('u', _userTurn.c_str(), _userTurnTs ? _userTurnTs : nowEpoch());
    _userTurn.clear();
    _userTurnTs = 0;
    _turnsSinceConsolidation++;
}

void AgentController::flushAgentTurn(bool interrupted) {
    if (_agentTurn.empty()) return;
    if (interrupted) _agentTurn += " ...";
    Serial.printf("[Chat] ai: %s\n", _agentTurn.c_str());
    _episodes.append('a', _agentTurn.c_str(), _agentTurnTs ? _agentTurnTs : nowEpoch());
    _agentTurn.clear();
    _agentTurnTs = 0;
}

// ============================================================
// State & status for the UI
// ============================================================

void AgentController::updateState(uint32_t now) {
    AvatarState s;
    if (_sleeping) s = AVATAR_SLEEP;
    else if (!_configOk) s = AVATAR_SETUP;
    else if (_fatalError || _holdUntil) s = AVATAR_ERROR;
    else if (!_wifiUp || !_gemini.isReady()) s = AVATAR_CONNECTING;
    else if (_audio->isPlaying()) s = AVATAR_SPEAKING;
    else if (!_muted && _gateAllowed && sinceMs(now, _lastVoice) < 400) s = AVATAR_LISTENING;
    else if ((int32_t)(_toolBusyUntil - now) > 0 || (_awaitingReply && sinceMs(now, _lastUserSpeech) < kThinkingTimeoutMs))
        s = AVATAR_THINKING;
    else s = AVATAR_IDLE;
    _state = (uint8_t)s;
    _face->setState(s);
    _face->setSpeechLevel(_audio->playbackLevel());
}

void AgentController::publishStatus() {
    UiStatus st;
    st.wifi = _wifiUp;
    st.rssi = _wifiUp ? WiFi.RSSI() : 0;
    st.battery = _battery;
    st.muted = _muted;
    st.configured = _configOk;
    st.memories = (uint32_t)_memory.size();
    st.reminders = (uint16_t)_scheduler.items().size();
    st.openTasks = (uint16_t)_tasks.openCount();
    st.tokensPerMin = tokensPerMinute();
    const uint32_t now = nowEpoch();
    if (now) {
        const timefmt::Civil c = timefmt::toCivil(now, TIME_UTC_OFFSET_SEC);
        snprintf(st.clock, sizeof(st.clock), "%02d:%02d", c.hour, c.minute);
    }
    if (gRadio.active()) {
        char name[64], title[128];
        gRadio.info(name, sizeof(name), title, sizeof(title));
        if (gRadio.state() != RadioPlayer::State::Playing) snprintf(st.radio, sizeof(st.radio), "%s (กำลังเชื่อมต่อ...)", name);
        else if (title[0]) snprintf(st.radio, sizeof(st.radio), "%s - %s", name, title);
        else snprintf(st.radio, sizeof(st.radio), "%s", name);
    }
    _face->setStatus(st);
}

void AgentController::updateInfo() {
    String s;
    char buf[160];
    const uint32_t sec = monoSec();
    snprintf(buf, sizeof(buf), "เวอร์ชัน %s • %s%s\n", FIRMWARE_VERSION, liveModelLabel(_activeModel),
             _liveSel.usingFallback(sec) ? " (สำรอง)" : "");
    s += buf;
    if (_wifiUp) {
        snprintf(buf, sizeof(buf), "Wi-Fi %s • %d dBm • %s\n", _cfg.wifiSsid, WiFi.RSSI(),
                 WiFi.localIP().toString().c_str());
    } else {
        snprintf(buf, sizeof(buf), "Wi-Fi: %s\n", _cfg.wifiSsid[0] ? "กำลังเชื่อมต่อ..." : "ยังไม่ตั้ง");
    }
    s += buf;
    const char* session = _sleeping ? "พัก" : (_gemini.isReady() ? "พร้อม" : (_gemini.started() ? "กำลังเชื่อมต่อ" : "ปิด"));
    snprintf(buf, sizeof(buf), "เซสชัน: %s • โทเคน/นาที %lu (ลิมิต 65k)\n", session, (unsigned long)tokensPerMinute());
    s += buf;
    if (_cfg.googleSearch && _searchOffUntil > sec) {
        snprintf(buf, sizeof(buf), "Google Search: โควตาหมด ปิดชั่วคราวอีก %lu นาที\n",
                 (unsigned long)((_searchOffUntil - sec + 59) / 60));
        s += buf;
    }
    if (_holdUntil) {
        snprintf(buf, sizeof(buf), "หยุดเชื่อมต่อชั่วคราว (%s) อีก %lu วิ\n", fallback::failureName(_liveSel.lastFailure()),
                 (unsigned long)(_holdUntil > sec ? _holdUntil - sec : 0));
        s += buf;
    }
    if (_lastClose.length()) {
        s += "ปิดล่าสุด: ";
        s += _lastClose;
        s += "\n";
    }
    if (_lastProbe.length()) {
        s += "ทดสอบ key: ";
        s += _lastProbe;
        s += "\n";
    }
    const ltm::Stats ms = _memory.stats();
    snprintf(buf, sizeof(buf), "ความจำ %u • กราฟ %u • งาน %u • แจ้งเตือน %u\n", (unsigned)ms.facts,
             (unsigned)ms.relations, (unsigned)_tasks.openCount(), (unsigned)_scheduler.items().size());
    s += buf;
    const uint32_t epoch = nowEpoch();
    snprintf(buf, sizeof(buf), "Flash-Lite วันนี้ %lu/%d ครั้ง • %s\n",
             (unsigned long)(epoch ? _consolidateLimit.usedToday(epoch) : 0), CONSOLIDATE_MAX_PER_DAY,
             textModelId(_textSel.active(sec)));
    s += buf;
    snprintf(buf, sizeof(buf), "RAM %u KB • PSRAM %u KB • %s ว่าง %u MB\n", ESP.getFreeHeap() / 1024,
             ESP.getFreePsram() / 1024, _storage->kindName(),
             (unsigned)((_storage->totalBytes() - _storage->usedBytes()) / (1024 * 1024)));
    s += buf;
    if (_battery >= 0) snprintf(buf, sizeof(buf), "แบตเตอรี่ %d%% • เปิดมา %lu นาที", _battery, millis() / 60000);
    else snprintf(buf, sizeof(buf), "ไฟจาก USB • เปิดมา %lu นาที", millis() / 60000);
    s += buf;
    xSemaphoreTake(_infoLock, portMAX_DELAY);
    _info = s;
    xSemaphoreGive(_infoLock);
}

void AgentController::uiSystemInfo(String& out) {
    xSemaphoreTake(_infoLock, portMAX_DELAY);
    out = _info;
    xSemaphoreGive(_infoLock);
}

// ============================================================
// DeviceOps
// ============================================================

uint32_t AgentController::nowEpoch() {
    const time_t t = time(nullptr);
    return t >= (time_t)timefmt::kMinValidEpoch ? (uint32_t)t : 0;
}

bool AgentController::setLedColor(const char* color) {
    struct Named {
        const char* name;
        uint32_t rgb;
    };
    static const Named kColors[] = {{"red", 0xFF0000},    {"green", 0x00FF00},  {"blue", 0x0000FF},
                                    {"white", 0xFFFFFF},  {"yellow", 0xFFB000}, {"purple", 0x8000FF},
                                    {"orange", 0xFF4000}, {"off", 0x000000}};
    if (!strcmp(color, "auto")) {
        _ledOverride = false;
        return true;
    }
    for (const Named& c : kColors) {
        if (!strcmp(color, c.name)) {
            _ledRgb = c.rgb;
            _ledOverride = true;
            return true;
        }
    }
    return false;
}

int AgentController::setVolume(int level) {
    gSettings.update([&](SettingsData& d) { d.volume = (uint8_t)level; });
    _audio->setVolume(level);
    _audio->playTone(660, 120, 4000);  // audible confirmation at the new level
    return level;
}

int AgentController::setBrightness(int level) {
    gSettings.update([&](SettingsData& d) { d.brightness = (uint8_t)level; });
    return level;
}

// ============================================================
// Internet radio: plays while the assistant sleeps, ducked while it listens / talks
// ============================================================

bool AgentController::stopRadio() {
    const bool was = gRadio.active();
    gRadio.stop();
    return was;
}

// play_radio: from the curated station list (number, name or genre; "" = last station)
const char* AgentController::playRadio(const char* station, JsonObject r) {
    if (!gStations.count()) return "ยังไม่มีสถานีในรายการ (เพิ่มได้ที่ ESP32-AI Studio แท็บวิทยุ)";
    const int i = (station && *station) ? gStations.find(station) : gStations.lastIndex();
    if (i < 0) return "ไม่มีสถานีนี้ในรายการ ให้เลือกจากรายชื่อสถานีในคำสั่งของคุณ";
    RadioStation s;
    if (!gStations.get((size_t)i, s) || !gStations.play((size_t)i)) return "เปิดสถานีไม่ได้";
    r["station"] = (const char*)s.name;
    r["number"] = i + 1;
    r["genre"] = (const char*)s.genre;
    r["note"] = "กำลังเปิด บอกชื่อสถานีสั้นๆ แล้วเงียบ (วิทยุดังขึ้นเมื่อคุณพูดจบ)";
    noteActivity();
    return nullptr;
}

void AgentController::handleRadio(uint32_t now, bool busy) {
    if (!gRadio.active()) {
        if (_radioWasOn) {  // stopped, or gave up after retries
            _radioWasOn = false;
            _audio->setRadioGain(1.0f);
            if (gRadio.state() == RadioPlayer::State::Error) {
                char msg[128];
                snprintf(msg, sizeof(msg), "เปิดวิทยุไม่ได้ (%s)", gRadio.lastError());
                _face->setHint(msg, 5000);
                if (!_sleeping) {
                    PsString a = "[แจ้งเตือนจากระบบ] ";
                    a += msg;
                    a += " — บอกผู้ใช้สั้นๆ";
                    _announcements.push_back(a);
                }
            }
        }
        return;
    }
    _radioWasOn = true;
    _audio->setRadioGain(_sleeping ? 1.0f : RADIO_DUCK_GAIN);
    // talked to the assistant and nothing more to say: back to the radio (session closes, no tokens)
    if (!_sleeping && !busy && !_vadOpen && !_awaitingReply && !_ringing && !_sleepPending &&
        sinceMs(now, _lastActivity) > RADIO_RESUME_MS) {
        enterSleep();
    }
}

int AgentController::volume() { return _audio->volume(); }
int AgentController::brightness() { return gSettings.get().brightness; }
int AgentController::micGain() { return gSettings.get().micGain; }

void AgentController::configureDevice(const DeviceChange& c, JsonObject r) {
    gSettings.update([&](SettingsData& d) {
        if (c.micGain >= 0) d.micGain = (uint8_t)c.micGain;
        if (c.sleepMinutes >= 0) d.sleepMinutes = (uint8_t)c.sleepMinutes;
        if (c.clockMinutes >= 0) d.clockMinutes = (uint8_t)c.clockMinutes;
        if (c.dimBrightness >= 0) d.dimBrightness = (uint8_t)c.dimBrightness;
    });
    const SettingsData s = gSettings.get();
    r["mic_sensitivity"] = s.micGain;
    r["sleep_minutes"] = s.sleepMinutes;
    r["clock_minutes"] = s.clockMinutes;
    r["dim_brightness"] = s.dimBrightness;
    if (c.powerSaveNow) {
        _powerSaveAfterSleep = true;
        requestSleep();  // after this reply, then straight to the clock face
        r["power_save"] = "after this reply: clock face + dim screen; voice or a tap wakes you";
    }
}

void AgentController::fillStatus(JsonObject out) {
    if (_battery >= 0) out["battery_percent"] = (int)_battery;
    else out["power"] = "usb";
    out["wifi_connected"] = (bool)_wifiUp;
    if (_wifiUp) out["wifi_rssi_dbm"] = WiFi.RSSI();
    out["speaker_volume"] = _audio->volume();
    if (gRadio.active()) {  // "เพลงอะไรอยู่" can be answered from here
        char rn[64], rt[128];
        gRadio.info(rn, sizeof(rn), rt, sizeof(rt));
        JsonObject ro = out["radio"].to<JsonObject>();
        ro["station"] = rn;
        ro["now_playing"] = rt;
        ro["state"] = gRadio.stateName();
    }
    out["screen_brightness"] = _cfg.brightness;
    out["mic_muted"] = (bool)_muted;
    out["live_model"] = liveModelId(_activeModel);
    if (_activeModel != _cfg.model) out["preferred_model_unavailable"] = liveModelId(_cfg.model);
    out["tokens_last_minute"] = tokensPerMinute();
    JsonObject st = out["storage"].to<JsonObject>();
    st["type"] = _storage->kindName();
    st["free_mb"] = (uint32_t)((_storage->totalBytes() - _storage->usedBytes()) / (1024 * 1024));
    out["uptime_minutes"] = millis() / 60000;
}

// Everything the PC app's status page shows (runs in the agent task via runTool("_diag"))
void AgentController::fillDiagnostics(JsonObject out) {
    static const char* const kStates[] = {"boot",     "connecting", "idle",  "listening", "thinking",
                                          "speaking", "error",      "sleep", "setup"};
    const uint32_t sec = monoSec();
    const uint8_t st = _state.load();
    out["fw"] = FIRMWARE_VERSION;
    out["state"] = st < sizeof(kStates) / sizeof(kStates[0]) ? kStates[st] : "?";
    out["session"] = _sleeping ? "sleep" : (_gemini.isReady() ? "ready" : (_gemini.started() ? "connecting" : "off"));
    out["wifi"] = (bool)_wifiUp;
    if (_wifiUp) {
        out["rssi"] = WiFi.RSSI();
        out["ip"] = WiFi.localIP().toString();
    }
    out["muted"] = (bool)_muted;
    out["sleeping"] = (bool)_sleeping;
    out["power_save"] = (bool)_powerSave;
    out["radio"] = gRadio.stateName();
    if (gRadio.active()) {
        char rn[64], rt[128];
        gRadio.info(rn, sizeof(rn), rt, sizeof(rt));
        out["radio_station"] = rn;
        out["radio_title"] = rt;
        out["radio_codec"] = gRadio.codec();
        out["radio_rate"] = gRadio.sampleRate();
    }
    if (gRadio.lastError()[0]) out["radio_error"] = gRadio.lastError();
    out["radio_echo_coupling"] = _coupling;  // mic level / radio level, learned while asleep
    out["radio_out_level"] = _audio->radioOutLevel();
    // reply stutter: gaps mid-reply, adaptive jitter buffer, output-loop and agent-loop hold-ups
    out["voice_underruns"] = _audio->voiceUnderruns();
    out["voice_last_gap_ms"] = _audio->lastGapMs();
    out["voice_prebuffer_ms"] = _audio->prebufferMs();
    out["audio_out_gap_max_ms"] = _audio->maxOutGapMs();
    out["agent_loop_max_ms"] = _loopMaxMs;
    out["gemini_dropped"] = _gemini.droppedMessages();
    out["ws_read_max_ms"] = _gemini.takeSlowReadMs();
    out["ws_write_max_ms"] = _gemini.takeSlowWriteMs();
    _audio->resetAudioStats();
    _loopMaxMs = 0;
    out["auth_error"] = _fatalError;
    out["error_text"] = _errorText;
    out["hold_s"] = _holdUntil > sec ? _holdUntil - sec : 0;
    out["last_failure"] = fallback::failureName(_liveSel.lastFailure());
    out["last_close"] = _lastClose;
    out["active_model"] = liveModelId(_activeModel);
    out["preferred_model"] = liveModelId(_cfg.model);
    out["google_search"] = searchAvailable();
    out["search_off_s"] = _searchOffUntil > sec ? _searchOffUntil - sec : 0;
    out["setup_mode"] = (uint8_t)_setupMode;
    JsonArray live = out["live_models"].to<JsonArray>();
    for (uint8_t k = 0; k < (uint8_t)LiveModel::Count; k++) {
        JsonObject m = live.add<JsonObject>();
        m["id"] = liveModelId(k);
        m["cooldown_s"] = _liveSel.cooldownLeft(k, sec);
    }
    out["text_model"] = textModelId(_textSel.active(sec));
    out["tokens_per_min"] = tokensPerMinute();
    const uint32_t epoch = nowEpoch();
    out["consolidations_today"] = epoch ? _consolidateLimit.usedToday(epoch) : 0;
    const ltm::Stats ms = _memory.stats();
    out["memories"] = (uint32_t)ms.facts;
    out["relations"] = (uint32_t)ms.relations;
    out["open_tasks"] = (uint32_t)_tasks.openCount();
    out["reminders"] = (uint32_t)_scheduler.items().size();
    out["volume"] = _audio->volume();
    out["mic_level"] = _audio->micLevel();
    out["heap_kb"] = ESP.getFreeHeap() / 1024;
    out["heap_min_kb"] = ESP.getMinFreeHeap() / 1024;
    out["psram_kb"] = ESP.getFreePsram() / 1024;
    out["storage"] = _storage->kindName();
    out["storage_free_mb"] = (uint32_t)((_storage->totalBytes() - _storage->usedBytes()) / (1024 * 1024));
    out["battery"] = (int)_battery;
    out["uptime_s"] = millis() / 1000;
    out["epoch"] = epoch;
}

void AgentController::describeTools(String& out) {
    JsonDocument doc(JsonPsramAllocator::instance());
    _tools->addDeclarations(doc.to<JsonArray>(), ToolBehavior::Unspecified, false);
    serializeJson(doc, out);
}

bool AgentController::setExpression(const char* emotion) {
    Emotion e;
    if (!parseEmotion(emotion, e)) return false;
    _face->setExpression(e, AGENT_EXPRESSION_HOLD_MS);
    return true;
}

bool AgentController::applyAssistantChange(const AssistantChange& c, JsonObject result) {
    gSettings.update([&](SettingsData& d) {
        if (c.profile) Settings::applyProfile(d, c.profile);
        bool custom = false;
        if (c.voice) {
            strncpy(d.voice, c.voice, sizeof(d.voice) - 1);
            custom = true;
        }
        if (c.personality >= 0) {
            d.personality = (uint8_t)c.personality;
            custom = true;
        }
        if (c.gender >= 0) {
            d.gender = (uint8_t)c.gender;
            custom = true;
        }
        if (c.eyeColor >= 0) d.eyeColor = (uint8_t)c.eyeColor;
        if (c.agentName) {
            strncpy(d.agentName, c.agentName, sizeof(d.agentName) - 1);
            d.agentName[sizeof(d.agentName) - 1] = '\0';
        }
        if (c.userName) {
            strncpy(d.userName, c.userName, sizeof(d.userName) - 1);
            d.userName[sizeof(d.userName) - 1] = '\0';
        }
        if (custom) strncpy(d.profile, "custom", sizeof(d.profile));
    });
    const SettingsData d = gSettings.get();
    result["agent_name"] = d.agentName;
    result["user_name"] = d.userName;
    result["voice"] = d.voice;
    result["gender"] = persona::genderId((persona::Gender)d.gender);
    result["personality"] = persona::kPersonalities[d.personality % persona::kPersonalityCount].id;
    result["profile"] = d.profile;
    if (c.voice || c.profile || c.personality >= 0 || c.gender >= 0 || c.agentName) {
        result["note"] = "Applied after this reply (the session restarts with the new voice/persona).";
    }
    return true;
}

void AgentController::describeAssistant(JsonObject out) {
    out["name"] = _cfg.agentName;
    if (_cfg.userName[0]) out["user_name"] = _cfg.userName;
    out["profile"] = _cfg.profile;
    out["voice"] = _cfg.voice;
    out["gender"] = persona::genderId((persona::Gender)_cfg.gender);
    out["personality"] = persona::kPersonalities[_cfg.personality % persona::kPersonalityCount].id;
    out["eye_color"] = persona::kEyeColors[_cfg.eyeColor % persona::kEyeColorCount].id;
    out["model"] = liveModelId(_activeModel);
}
