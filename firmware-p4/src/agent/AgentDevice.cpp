/**
 * @file AgentDevice.cpp
 * @brief AgentController: UI state and status, DeviceOps (voice device control), Internet radio, diagnostics
 * @see wiki/04-System-Architecture.md
 */

#include "AgentInternal.h"

#include <esp_random.h>

// ============================================================
// State & status for the UI
// ============================================================

void AgentController::updateState(uint32_t now) {
    if (_audio->isPlaying()) {
        _lastAgentAudio = now;
        noteActivity();
    }
    AvatarState s;
    if (_sleeping) s = AVATAR_SLEEP;
    else if (!_configOk) s = AVATAR_SETUP;
    else if (_fatalError || _holdUntil) s = AVATAR_ERROR;
    else if (!_wifiUp || !_gemini.isReady()) s = AVATAR_CONNECTING;
    else if (_audio->isPlaying()) s = AVATAR_SPEAKING;
    else if (!_muted && _gateAllowed && sinceMs(now, _lastVoice) < 400) s = AVATAR_LISTENING;
    else if (_toolBusy.active(now) ||
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
    const webtools::ClockWeather wx = webtools::getClockWeather();
    st.wxValid = wx.valid;
    st.wxTemp = wx.temp;
    st.wxHumidity = wx.humidity;
    st.wxPm25 = wx.pm25;
    st.wxCode = wx.code;
    st.wxIsDay = wx.isDay;
    snprintf(st.wxCity, sizeof(st.wxCity), "%s",
             wx.city[0] ? wx.city : (_cfg.weatherCity[0] ? _cfg.weatherCity : "อัตโนมัติ"));
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
        snprintf(buf, sizeof(buf), "Wi-Fi %s • %d dBm • %s\n", _wifiTarget, WiFi.RSSI(),  // maybe a remembered one
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

// set_volume: the radio only - the voice stays at aiVolume (a confirmation tone would play at the voice's
// level, not the radio's: none; the radio itself is the feedback)
int AgentController::setVolume(int level) {
    gSettings.update([&](SettingsData& d) { d.volume = (uint8_t)level; });
    _audio->setRadioVolume(level);
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
    gRadioUi.requestClose();  // "ปิดวิทยุ" = off and back to the face, like ⏻ (it stayed on the paused screen)
    return was;
}

// play_radio: from the curated station list (number, name or genre; "" = last station)
const char* AgentController::playRadio(const char* station, JsonObject r) {
    if (!gStations.count()) return "ยังไม่มีสถานีในรายการ (เพิ่มได้ที่ ESP32-AI Studio แท็บวิทยุ)";
    // "เปิดวิทยุ" without a station = the last one played. Models tend to fill the argument anyway
    // ("วิทยุ", "ล่าสุด", "last"): such words mean "no station".
    static const char* const kGeneric[] = {"วิทยุ", "radio", "ล่าสุด", "สถานีล่าสุด", "สถานีเดิม", "เดิม", "last",
                                           "previous", "default", "ค่าเริ่มต้น"};
    String q = station ? station : "";
    q.trim();
    q.toLowerCase();
    for (const char* g : kGeneric) {
        if (q == g) q = "";
    }
    const int i = q.length() ? gStations.find(q.c_str()) : gStations.lastIndex();
    if (i < 0) return "ไม่มีสถานีนี้ในรายการ ให้เลือกจากรายชื่อสถานีในคำสั่งของคุณ";
    RadioStation s;
    char playing[64] = "";
    if (gRadio.active()) gRadio.info(playing, sizeof(playing), nullptr, 0);
    if (playing[0] && gStations.get((size_t)i, s) && !strcmp(playing, s.name)) {
        // the model re-called it while answering something else ("ราคาทอง" got "เปิด 93.5 ให้แล้ว..."),
        // and a friendly "already playing" note was ignored: refuse it as an error, no restart
        return "ไม่ได้ทำอะไร: สถานีนี้เล่นอยู่แล้วและผู้ใช้ไม่ได้ขอเปิดวิทยุ ห้ามพูดว่าเปิดวิทยุ ตอบเฉพาะเรื่องที่ผู้ใช้ถาม";
    }
    if (!gStations.get((size_t)i, s) || !gStations.play((size_t)i)) return "เปิดสถานีไม่ได้";
    r["station"] = (const char*)s.name;
    r["number"] = i + 1;
    r["genre"] = (const char*)s.genre;
    r["note"] = "กำลังเปิด บอกชื่อสถานีสั้นๆ แล้วเงียบ (คุณจะพักฟังวิทยุทันทีที่พูดจบ ผู้ใช้เรียกได้ด้วยเสียงหรือแตะจอ)";
    noteActivity();
    // Sleep right after the confirmation: the radio plays at full level and the mic only listens for
    // a wake-up (echo-aware). Awake, the follow-up window streamed the DJ to the model as the user.
    requestSleep();  // (the next wake starts a fresh session: enterSleep)
    return nullptr;
}

// play_radio with "music" (T122): songs from /sdcard/music through the radio's player - the same ducking, wake
// and back-to-sleep rules as a station
const char* AgentController::playMusic(const char* query, JsonObject r) {
    if (!gMusic.available()) return "ไม่มี SD card จึงเล่นเพลงไม่ได้ (ใส่ SD card ที่มีไฟล์ .mp3 ในโฟลเดอร์ music)";
    if (!gMusic.count() && !gMusic.scan()) return "ยังไม่มีไฟล์เพลง (.mp3) ในโฟลเดอร์ music บน SD card";
    String q = query ? query : "";
    q.trim();
    static const char* const kAny[] = {"*", "เพลง", "music", "mp3", "ต่อ", "เล่นต่อ", "เพลงต่อ", "all", "ทั้งหมด"};
    // T124 (board logs): "เปลี่ยนเพลง" came as "*" every time = carry on = the same song again
    static const char* const kNext[] = {"next", "ถัดไป", "เพลงถัดไป", "เปลี่ยน", "เปลี่ยนเพลง", "เพลงอื่น", "อื่น",
                                        "skip", "ข้าม", "another", "change"};
    static const char* const kPrev[] = {"prev", "previous", "back", "ก่อนหน้า", "เพลงก่อนหน้า", "ย้อน", "เพลงที่แล้ว"};
    static const char* const kRandom[] = {"random", "shuffle", "สุ่ม", "สุ่มเพลง"};
    int step = 0;
    bool random = false;
    for (const char* g : kAny) {
        if (q.equalsIgnoreCase(g)) q = "";
    }
    for (const char* g : kNext) {
        if (q.equalsIgnoreCase(g)) q = "", step = 1;
    }
    for (const char* g : kPrev) {
        if (q.equalsIgnoreCase(g)) q = "", step = -1;
    }
    for (const char* g : kRandom) {
        if (q.equalsIgnoreCase(g)) q = "", random = true;
    }
    // asking for songs while one plays is asking for another one (carrying on needs no call)
    if (!q.length() && !step && !random && gMusic.playing()) step = 1;
    if (random) {
        const int n = (int)gMusic.count();
        int i = (int)(esp_random() % (uint32_t)n);
        if (n > 1 && i == gMusic.current()) i = (i + 1) % n;
        if (!gMusic.play(i)) return "เปิดเพลงไม่ได้";
    } else if (step) {
        if (!gMusic.step(step)) return "เปิดเพลงไม่ได้";
    } else if (q.length()) {
        const int i = gMusic.find(q.c_str());
        if (i < 0) return "ไม่พบเพลงนี้ใน SD card (ค้นจากชื่อไฟล์เพลง) บอกผู้ใช้ตรงๆ";
        if (!gMusic.play(i)) return "เปิดเพลงไม่ได้";
    } else if (!gMusic.playing() && !gMusic.resume()) {
        return "เปิดเพลงไม่ได้";
    }
    const int cur = gMusic.current();
    char name[160] = "";
    gMusic.get((size_t)cur, name, sizeof(name));
    r["song"] = (const char*)name;
    r["number"] = cur + 1;
    r["of"] = (unsigned)gMusic.count();
    r["note"] = "กำลังเล่นเพลงนี้จาก SD card บอกชื่อเพลงสั้นๆ แล้วเงียบ (จบเพลงแล้วเล่นเพลงถัดไปเอง)";
    noteActivity();
    requestSleep();  // like a station: the song plays at full level, the mic only listens for a wake-up
    return nullptr;
}

void AgentController::handleRadio(uint32_t now, bool busy) {
    if (!gRadio.active()) {
        if (_radioWasOn) {  // stopped, or gave up after retries
            _radioWasOn = false;
            _audio->setRadioGain(1.0f);
            if (gRadio.state() == RadioPlayer::State::Error) {
                char msg[128];
                // T125: a song from the card is not "the radio" (it said "เปิดวิทยุไม่ได้" right after naming a song)
                snprintf(msg, sizeof(msg), gRadio.isFile() ? "เล่นเพลงจาก SD card ไม่ได้ (%s)" : "เปิดวิทยุไม่ได้ (%s)",
                         gRadio.lastError());
                _face->setHint(msg, 5000);
                if (!_sleeping) {
                    PsString a = "[แจ้งเตือนจากระบบ] ";
                    a += msg;
                    a += " — บอกผู้ใช้สั้นๆ";
                    announce(a);
                }
            }
        }
        return;
    }
    if (!_radioWasOn && !_sleeping && !busy && !_vadOpen && !_awaitingReply) {
        // started from the screen / console while awake and idle: listen to the radio now (asleep
        // the mic only waits for a wake-up; awake it would stream the radio as "user speech")
        requestSleep();
    }
    _radioWasOn = true;
    _audio->setRadioGain(_sleeping ? 1.0f : RADIO_DUCK_GAIN);
    // talked to the assistant and nothing more to say: back to the radio (session closes, no tokens)
    // T119 (seen on the board): "weather + news" with the radio on - the "let me check" reply ended, 6 s later the
    // session closed under the lookups and both results were dropped. Wait for them, then for the answer (15 s cap).
    const bool toolAnswerDue = _webPending || (_awaitToolAnswer && sinceMs(now, _toolAnswerAt) < 15000);
    if (!_sleeping && !busy && !_vadOpen && !_awaitingReply && !_ringing && !_sleepPending && !toolAnswerDue &&
        sinceMs(now, _lastActivity) > RADIO_RESUME_MS) {
        enterSleep();
    }
}

int AgentController::volume() { return _audio->radioVolume(); }
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
    // set_volume changes the radio only (T068): report that one, the voice is locked at its own level
    out["radio_volume"] = _audio->radioVolume();
    out["voice_volume_locked"] = _audio->volume();
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
    out["radio_echo_coupling"] = _echoGate.coupling();  // mic level / reference level, learned while asleep
    out["echo_ref"] = _audio->hasEchoRef() ? "dac_loopback" : "software";
    out["echo_envelope"] = _echoGate.envelope();
    out["echo_peak_ratio"] = _echoGate.takePeakRatio();  // music alone vs the wake factor (margin)
    if (_aec) {  // echo removal: how much it takes off, what is left, talk detected over the speaker, cost
        out["aec_erle_db"] = _aec->erleDb();
        out["aec_residual"] = _aec->residualFloor();
        out["aec_double_talk"] = _aecDoubleTalk;
        out["aec_max_us"] = _aecMaxUs;  // per 40-ms chunk
        out["aec_bypass"] = _aec->takeBypasses();  // chunks passed through: the filter would have made them louder
        out["aec_resets"] = _aec->takeResets();
        _aecDoubleTalk = 0;
        _aecMaxUs = 0;
    }
    // what wakes it by voice (0 loudness, 1 wake word, 2 both) and the ESP-SR wake word engine (wiki 14)
    out["wake_mode"] = _cfg.wakeMode;
    out["wake_word"] = _wakeWord.ready() ? _wakeWord.model() : "";
    if (_wakeWord.ready()) {
        out["wake_word_heard"] = _wakeWord.detections();
        out["wake_word_dbfs"] = _wakeWord.lastVolumeDb();
        out["wake_word_dropped"] = _wakeWord.dropped();
        out["wake_word_peak_dbfs"] = _wakeWord.takePeakDb();  // loudest since the last diag
        out["wake_word_threshold"] = _wakeWord.threshold();    // 0 = the model's default  // samples: the AFE fell behind (should stay 0)
    }
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
    if (gPhone.paired()) gPhone.describe(out["phone"].to<JsonObject>());  // the phone app (wiki 18)
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
    out["heap_largest_kb"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;  // T119: fragmentation
    out["psram_kb"] = ESP.getFreePsram() / 1024;
    out["storage"] = _storage->kindName();
    out["sd_remounts"] = _storage->remounts();  // T125: times a card that stopped answering was mounted again
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

// ============================================================
// Devices on the free ports (system/Peripherals, agent/devices.json - wiki 15)
// ============================================================

int AgentController::deviceCount() { return gPeriph.count(); }

void AgentController::listDevices(JsonArray out) { gPeriph.describe(out); }

const char* AgentController::deviceAction(const char* action, const char* name, JsonVariantConst value,
                                          JsonObject result) {
    if (!strcmp(action, "set")) return gPeriph.set(name, value, result);
    return gPeriph.read(name, result);
}

void AgentController::reloadDevices() {
    char path[96];
    snprintf(path, sizeof(path), "%s/devices.json", _storage->agentDir());
    gPeriph.begin(_storage->kind() != StorageKind::None ? path : "");
    if (_gemini.started()) startFreshSession(nullptr);  // the tool list and the prompt changed
}

// an input with "notify" changed: say it (a doorbell / PIR / door contact). Asleep it wakes up for it.
void AgentController::handleDevices(uint32_t now) {
    gPeriph.poll(now, [this](const char* name, bool active) {
        Serial.printf("[Devices] %s -> %s\n", name, active ? "active" : "idle");
        PsString msg = "[แจ้งเตือนจากระบบ] อุปกรณ์ \"";
        msg += name;
        msg += active ? "\" ทำงาน (active)" : "\" กลับเป็นปกติ (idle)";
        msg += " — บอกผู้ใช้สั้นๆ";
        announce(msg);
        noteActivity();
        if (_sleeping) wake(false);
    });
}

// ---- vehicle dash (wiki 16) ----
bool AgentController::vehicleEnabled() { return true; }  // always declared: "เปิดหน้าปัด" works before a source is set

const char* AgentController::vehicleAction(const char* action, const char* source, JsonObject r) {
    bool simStarted = false, full = false;
    const char* note = nullptr;
    if (!strcmp(action, "open")) {
        simStarted = gVehicle.openDash();
    } else if (!strcmp(action, "close")) {
        gVehicle.requestClose();
    } else if (!strcmp(action, "set_source")) {
        vehicle::SourceId id;
        if (!vehicle::parseSource(source, id)) return "unknown source";
        if (const char* why = gVehicle.setSource(id)) return why;
        gVehicle.requestOpen();
    } else if (!strcmp(action, "scan")) {  // a fresh scan is deferred (deferVehicleCall): here = what is known
        full = true;
        if (!gVehicle.diag().available) {
            note = "this source cannot read trouble codes: an OBD-II ELM327 adapter on the UART (source obd_uart) can; "
                   "the health findings below come from the live readings";
        } else if (!gVehicle.requestScan()) {
            note = "not connected to the car's ECU right now (ignition off?): the last codes read are below";
        }
    } else if (!strcmp(action, "clear_codes")) {
        uint32_t seq;
        if (const char* why = gVehicle.requestClear(seq)) return why;
        full = true;
        note = "clearing was requested; the codes below are from before it";
    }
    gVehicle.describe(r, full);
    if (!r["replay"].isNull()) {  // T115
        note = "a drive recorded on the SD card (replay.file), played back - not the car now; tell the user so";
    } else if (gVehicle.status() == "BT OFF") {  // T114: Bluetooth OBD follows the dash screen
        note = "Bluetooth to the OBD adapter is off while the dash is closed (it follows the dash screen): call vehicle "
               "with action open, then ask again in ~10 s";
    }
    if (simStarted) {
        r["note"] = "no data source was set up, so the dash shows the built-in simulator (not the user's vehicle); "
                    "a real car / motorcycle is chosen in Settings > หน้าปัดรถ";
    } else if (note) {
        r["note"] = note;
    } else if (r["simulated"] | false) {
        r["note"] = "simulated data, not the user's real vehicle";
    }
    return nullptr;
}

// vehicle scan / clear_codes: the car is asked now, the answer goes with the next tool results (like a web lookup)
bool AgentController::deferVehicleCall(JsonObjectConst call) {
    const char* action = call["args"]["action"] | "";
    const bool clear = !strcmp(action, "clear_codes");
    if ((!clear && strcmp(action, "scan")) || _vehJob.active) return false;
    uint32_t seq = 0;
    if (clear) {
        if (gVehicle.requestClear(seq)) return false;  // refused (engine running...): answered now with the reason
    } else {
        seq = gVehicle.requestScan();
    }
    if (!seq) return false;  // no codes on this source / not connected: answered now with what is known
    _vehJob.active = true;
    _vehJob.clear = clear;
    _vehJob.seq = seq;
    _vehJob.at = millis();
    _vehJob.gen = _sessionGen;
    snprintf(_vehJob.id, sizeof(_vehJob.id), "%s", call["id"] | "");
    if (_webPending < 255) _webPending++;
    Serial.printf("[Tool] vehicle %s -> asking the car...\n", action);
    _face->setHint(clear ? "กำลังล้างรหัสปัญหา..." : "กำลังสแกนรถ...", 3000);
    _toolBusy.arm(millis(), 15000);
    return true;
}

void AgentController::finishVehicleJob(bool done) {
    _vehJob.active = false;
    if (_vehJob.gen != _sessionGen || !_gemini.isReady()) {  // (a new session counts its own lookups from 0)
        Serial.println("[Tool] vehicle result dropped (session changed)");
        return;
    }
    if (_webPending) _webPending--;
    if (!_toolBatch.size()) _toolBatchAt = millis();
    JsonObject fr = _toolBatch["toolResponse"]["functionResponses"].add<JsonObject>();
    if (_vehJob.id[0]) fr["id"] = _vehJob.id;
    fr["name"] = "vehicle";
    JsonObject r = fr["response"]["result"].to<JsonObject>();
    gVehicle.describe(r, true);
    if (!done) r["note"] = "the car did not finish answering in time: these are the last results read";
    else if (r["simulated"] | false) r["note"] = "simulated data, not the user's real vehicle";
    Serial.printf("[Tool] vehicle %s -> %s (%.1f s)\n", _vehJob.clear ? "clear_codes" : "scan", done ? "ok" : "timeout",
                  (millis() - _vehJob.at) / 1000.0f);  // sent by handleWebResults with any web results
    _toolBusy.arm(millis(), 600);
}

// a warning that holds for a few seconds (engine too hot, not charging, new code): say it, then not again for a while
void AgentController::handleVehicle(uint32_t now) {
    if (_vehJob.active) {
        const bool done = gVehicle.scanDone(_vehJob.seq);
        if (done || now - _vehJob.at > 15000) finishVehicleJob(done);
    }
    static uint32_t next = 0;
    if ((int32_t)(now - next) < 0) return;
    next = now + 1000;
    gVehicle.service();  // the protocol / baud rate the ELM327 found (NVS: this task's internal stack)
    String msg;
    if (!gVehicle.takeAlert(msg, now)) return;
    Serial.printf("[Vehicle] Alert: %s\n", msg.c_str());
    PsString m = "[แจ้งเตือนรถจากระบบ] ";
    m += msg.c_str();
    m += " — บอกผู้ใช้สั้นๆ";
    announce(m);
    _face->setSystemText(msg.c_str(), 15000);
    noteActivity();
    if (_sleeping) wake(false);
}

// ---- the phone app: GPS + Google Maps (wiki 18) ----
bool AgentController::phonePaired() { return gPhone.paired(); }

const char* AgentController::navigateAction(const char* action, const char* to, JsonObject r) {
    if (!strcmp(action, "go") || !strcmp(action, "stop")) {
        if (!gPhone.connected()) {
            return "the phone is not connected: open ESP32-AI Flasher on the phone (นำทาง tab), on the same Wi-Fi / hotspot";
        }
        if (!gPhone.request(!strcmp(action, "go") ? "navigate" : "stop", to)) return "could not reach the phone";
        r["status"] = !strcmp(action, "go")
                          ? "sent to the phone: Google Maps starts navigating; you will announce each turn"
                          : "sent to the phone: navigation stops";
        if (*to) r["to"] = to;
    }
    gPhone.describe(r);
    return nullptr;
}

// the next turn from Google Maps (and a failure from the phone): said by the assistant, the turn first
void AgentController::handleNavigation(uint32_t now) {
    static uint32_t next = 0;
    static int declared = -1;  // the `navigate` tool is declared only once a phone is paired
    if ((int32_t)(now - next) < 0) return;
    next = now + 500;
    const int on = gPhone.paired() ? 1 : 0;
    if (declared >= 0 && on != declared && _gemini.started()) {
        _restartPending = true;  // the tool list changed: fresh session after the current reply
        _restartAt = millis() + 1500;
        _resumeHandle = "";
    }
    declared = on;
    String msg;
    bool turn = false;
    if (!gPhone.takeSay(msg, turn, now)) return;
    Serial.printf("[Phone] Say: %s\n", msg.c_str());
    PsString m = turn ? "[นำทางจากระบบ] " : "[แจ้งจากมือถือ] ";
    m += msg.c_str();
    m += turn ? " — พูดบอกผู้ใช้สั้นๆ ประโยคเดียว ไม่ต้องถามต่อ" : " — บอกผู้ใช้สั้นๆ";
    if (turn) {
        _navSay = m;  // replaces a turn not said yet: only the latest one matters
        _navSayAt = now;
    } else {
        announce(m);
    }
    _face->setSystemText(msg.c_str(), 10000);
    noteActivity();
    if (_sleeping) wake(false);
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

void AgentController::describeBoard(JsonObject out) {
    out["firmware"] = FIRMWARE_VERSION;  // changes with every release: about_me needs no "last updated" text
    out["hardware"] = "ES3C28P desk device: ESP32-S3, 2.8-inch touch screen with your eyes, speaker, microphone, "
                      "RGB LED, SD card; talks through Gemini Live, memory stays on the device";
    out["storage"] = _storage->kindName();
    JsonArray wake = out["wake"].to<JsonArray>();
    wake.add("your name");
    if (_cfg.wakeMode != 0) {  // same wake words as the session prompt
        if (_cfg.wakeWord != 0) wake.add("Hi ESP (offline)");
        if (_cfg.wakeWord != 1) wake.add("Jarvis (offline)");
    }
}
