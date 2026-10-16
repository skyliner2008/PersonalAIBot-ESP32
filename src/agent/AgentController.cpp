/**
 * @file AgentController.cpp
 * @brief Personal assistant orchestration: setup, settings, main loop
 *        (the rest of the class: AgentSession.cpp, AgentMic.cpp, AgentRoutines.cpp, AgentDevice.cpp)
 * @see wiki/04-System-Architecture.md
 */

#include "AgentInternal.h"

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
    if (void* mem = psramMalloc(sizeof(echo::Canceller))) _aec = new (mem) echo::Canceller();  // internal RAM is scarce
    // ~30 KB of internal RAM: only when the wake word is wanted (needs the "model" partition; without
    // it loudness wakes it, as before). Choosing it later (`set wake`, menu) starts it then.
    if (gSettings.get().wakeMode != 0 && _wakeWord.begin(gSettings.get().wakeWord ? "hiesp" : "jarvis", WAKE_WORD_OWN_AEC)) {
        _wakeWord.setThreshold(gSettings.get().wakeThreshold / 100.0f);
    }
    {  // a confirmed Search quota outlives the reboot (else every boot hits it again)
        Preferences rt;
        if (rt.begin("rt", false)) {  // read-write: creates the namespace on first boot (no NOT_FOUND log)
            _search.restore(rt.isKey("srchoff") ? rt.getUInt("srchoff", 0) : 0,
                            rt.isKey("srchn") ? rt.getUChar("srchn", 0) : 0);
            rt.end();
        }
    }

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

// 9 KB: `mem` over a 1-hour soak with tools, radio and sessions showed 9.2 of 12 KB never touched
// (internal RAM goes to the wake word - wiki 14). Writes flash (settings, LittleFS): stays internal.
void AgentController::startTask() { xTaskCreatePinnedToCore(taskEntry, "agent", 9216, this, 4, nullptr, 1); }

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
                                n.financeTools != _cfg.financeTools;

    if (initial || n.volume != _cfg.volume) _audio->setVolume(n.volume);
    if (initial || n.micGain != _cfg.micGain) _audio->setMicGain(n.micGain);
    if (!initial && n.wakeMode != 0 && _cfg.wakeMode == 0) _wakeWord.begin(n.wakeWord ? "hiesp" : "jarvis", WAKE_WORD_OWN_AEC);
    if (_wakeWord.ready() && (initial || n.wakeThreshold != _cfg.wakeThreshold || (n.wakeMode != 0 && _cfg.wakeMode == 0))) {
        _wakeWord.setThreshold(n.wakeThreshold / 100.0f);
    }
    if ((initial || n.brightness != _cfg.brightness) && !_powerSave) gUi.setBrightness(n.brightness);
    if (_powerSave && n.dimBrightness != _cfg.dimBrightness) gUi.setBrightness(n.dimBrightness);
    if (initial || n.eyeColor != _cfg.eyeColor) _face->setEyeColor(eyeColor565(n.eyeColor));
    gUi.setTouchTransform(n.touchMap);
    _tools->setFinancePack(n.financeTools);
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
        gOta.heartbeat();  // a fresh firmware update is confirmed only while this loop runs
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
        handleSearchGate();
        handleConsolidation(now);
        lap("consolidation");
        updateState(now);
        reportReply();
        lap("state");
        const uint32_t took = millis() - loopStart;
        if (took > _loopMaxMs) _loopMaxMs = took;
        if (took > 150) {
            Serial.printf("[Agent] loop held %lu ms (slowest: %s %lu ms)\n", (unsigned long)took, slow,
                          (unsigned long)slowMs);
        }
        if (deadline::reached(now, _nextStatus)) {
            _nextStatus = now + 250;
            publishStatus();
        }
        if (deadline::reached(now, _nextInfo)) {
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
                if (!_search.available(monoSec(), nowEpoch())) {
                    if (_search.reset()) saveSearchState(0, 0);
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
                _setupMode = (SetupMode)(_requestedSetupMode > 3 ? 0 : _requestedSetupMode);
                _liveSel.reset();
                _resumeHandle = "";
                retryNow();
                if (_gemini.started()) _gemini.restart();
                Serial.printf("[Agent] Setup mode %u (0 full, 1 no search, 2 minimal, 3 search only)\n", (unsigned)_setupMode);
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
