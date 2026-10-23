/**
 * @file AgentRoutines.cpp
 * @brief AgentController: sleep / wake / power save, reminders, memory consolidation, episodic log
 * @see wiki/04-System-Architecture.md
 */

#include "AgentInternal.h"

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
    _prerollDraining = false;
    _wakeVotes = 0;
    _preroll.clear();
    if (_gemini.started()) _gemini.stop();  // no session = no tokens
    // every wake starts a fresh session (a dropped link still resumes): a resumed one replayed the last
    // request when only the wake word came in - the radio again, "พักหน้าจอ" again, the weather again
    // (T051, T066). Long-term memory and recall_conversation keep the continuity.
    _resumeHandle = "";
    _sleepSince = millis();
    _radioSettle.arm(_sleepSince, 2500);  // radio ramps back to full: relearn its echo before listening
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
    // a "sleep" asked while already asleep stayed pending and put it straight back to sleep on the next
    // wake (the wake word woke it and it slept in the same second)
    _sleepPending = false;
    // Keep what is said while the session connects (2-4 s) and replay it once ready - also after a
    // tap: users start talking right away and lost their first words. After a tap only what comes
    // next counts, and it is sent only if there was voice in it.
    _wakeBuffering = true;
    _wakeHeard = byVoice;
    _wakeVoiced = byVoice ? AGENT_WAKE_CHUNKS : 0;  // the chunks that woke it are speech too
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
        if (deadline::reached(now, _ringUntil)) {
            _ringing = false;
        } else if (deadline::reached(now, _nextChime) && !_audio->isPlaying()) {
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
    if (!deadline::reached(now, _nextSchedulerCheck)) return;
    _nextSchedulerCheck = now + 1000;
    const uint32_t epoch = nowEpoch();
    if (!epoch) return;
    std::vector<sched::Reminder> fired;
    if (!_scheduler.collectDue(epoch, fired)) return;
    for (const sched::Reminder& r : fired) {  // r.due = when it was meant to ring
        // Rang late (the board was off / unplugged when it was due): say so, with the time it was set
        // for - announcing yesterday's appointment as "it is time" misled the user.
        const bool late = epoch - r.due > kLateReminderSec;
        Serial.printf("[Reminder] %s%s: %s\n", r.timer ? "timer" : "reminder", late ? " (missed)" : "", r.label.c_str());
        PsString msg = "[แจ้งเตือนจากระบบ] ";
        if (late) {
            const timefmt::Civil c = timefmt::toCivil(r.due, TIME_UTC_OFFSET_SEC);
            char when[64];
            snprintf(when, sizeof(when), "%s ที่ตั้งไว้ %02d/%02d %02d:%02d น. ผ่านไปแล้ว (ตอนนั้นเครื่องปิดอยู่): ",
                     r.timer ? "ตัวจับเวลา" : "การแจ้งเตือน", c.day, c.month, c.hour, c.minute);
            msg += when;
        } else {
            msg += r.timer ? "ตัวจับเวลาครบแล้ว: " : "ถึงเวลา: ";
        }
        msg += r.label;
        msg += late ? " — บอกผู้ใช้สั้นๆ ว่าพลาดไป" : " — บอกผู้ใช้สั้นๆ";
        announce(msg);
        PsString shown = late ? "แจ้งเตือนที่พลาดไป: " : "แจ้งเตือน: ";
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

void AgentController::announce(const PsString& text) {
    if (_announcements.size() >= kMaxAnnouncements) {
        Serial.printf("[Agent] announcement queue full - oldest dropped: %.60s\n", _announcements.front().text.c_str());
        _announcements.erase(_announcements.begin());
    }
    _announcements.push_back({text, millis()});
}

void AgentController::deliverAnnouncements() {
    if (!_navSay.empty() && (uint32_t)(millis() - _navSayAt) > 15000) {
        Serial.println("[Phone] turn not said in time - dropped (the phone's Maps says it too)");
        _navSay.clear();
    }
    while (!_announcements.empty() && (uint32_t)(millis() - _announcements.front().at) > kAnnouncementTtlMs) {
        Serial.printf("[Agent] announcement too old - dropped: %.60s\n", _announcements.front().text.c_str());
        _announcements.erase(_announcements.begin());
    }
    if ((_announcements.empty() && _navSay.empty()) || !_gemini.isReady() || _audio->isPlaying() || _vadOpen ||
        _awaitingReply) {
        return;
    }
    const bool turn = !_navSay.empty();  // a turn cannot wait behind a reminder
    const PsString& next = turn ? _navSay : _announcements.front().text;
    if (_gemini.sendClientText(next.c_str(), true)) {
        Serial.printf("[Chat] sent text turn (%u bytes)\n", (unsigned)next.size());
        if (turn) _navSay.clear();
        else _announcements.erase(_announcements.begin());
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
            if (_pendingUntil > _consolidatedUntil) {
                _consolidatedUntil = _pendingUntil;
                Preferences rt;  // kept across reboots (NVS "rt"): those turns are not sent again
                if (rt.begin("rt", false)) {
                    rt.putUInt("consol", _consolidatedUntil);
                    rt.end();
                }
            }
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
