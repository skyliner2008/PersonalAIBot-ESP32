/**
 * @file AgentDevice.cpp
 * @brief AgentController: UI state and status, DeviceOps (voice device control), Internet radio, diagnostics
 * @see wiki/04-System-Architecture.md
 */

#include "AgentInternal.h"

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
    else if ((int32_t)(_toolBusyUntil - now) > 0 ||
             (_awaitingReply &&
              sinceMs(now, _lastUserSpeech) < (_searchInSetup ? kSearchThinkingTimeoutMs : kThinkingTimeoutMs)))
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
    if (_cfg.googleSearch && !_search.available(sec, nowEpoch())) {
        const uint32_t left = _search.secondsLeft(sec, nowEpoch());
        if (_search.confirmed(sec, nowEpoch())) {
            if (left) snprintf(buf, sizeof(buf), "Google Search: โควตาหมด ปิดชั่วคราวอีก %lu นาที\n", (unsigned long)((left + 59) / 60));
            else snprintf(buf, sizeof(buf), "Google Search: โควตาหมด (รอเวลาจากเน็ต)\n");
        } else {
            snprintf(buf, sizeof(buf), "Google Search: กำลังตรวจ (ลองต่อแบบไม่มี Search)\n");
        }
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
    out["search_off_s"] = _search.secondsLeft(sec, nowEpoch());
    out["search_state"] = _search.suspect(sec) ? "suspect" : _search.confirmed(sec, nowEpoch()) ? "quota" : "ok";
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
