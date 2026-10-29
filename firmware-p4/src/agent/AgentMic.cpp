/**
 * @file AgentMic.cpp
 * @brief AgentController: microphone gate, local VAD + AGC, wake detection
 * @see wiki/04-System-Architecture.md
 */

#include "AgentInternal.h"

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

// T113: the mic chunks below are PSRAM (made on first use): 5 KB of internal RAM back for TLS / Bluetooth
void AgentController::sendMic(const int16_t* pcm, size_t n) {
    static int16_t* const out = (int16_t*)psramMalloc(kChunk * sizeof(int16_t));
    if (n > kChunk) n = kChunk;
    for (size_t i = 0; i < n; i++) {
        float v = pcm[i] * _agcGain;
        out[i] = (int16_t)(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : v));
    }
    _gemini.sendAudio(out, n, AUDIO_UPLOAD_RATE);
    _uttChunks++;
}

// The ring can hold up to 6 s (what was said while the session connected) but the send queue only
// takes ~2.5 s and mic audio never waits for a slot: sent in one go, the end of it was dropped. So it
// goes out as fast as the queue drains; live chunks queue up behind it in the ring (order kept).
void AgentController::sendPreroll() {
    // what was said while the session connected counts as speech of this sentence: uncounted, a call
    // from 20-30 cm (all of it said during the 3-4 s of connecting) ended as "too short", nothing waited
    // for the answer and the radio rule put the assistant back to sleep before Gemini replied
    if (_wakeBuffering) {
        _uttVoiced += _wakeVoiced;
        _wakeVoiced = 0;
    }
    _wakeBuffering = false;
    _vadOpen = true;
    _lastVoice = millis();
    _prerollDraining = true;
    drainPreroll();
}

void AgentController::drainPreroll() {
    static int16_t* const chunk = (int16_t*)psramMalloc(kChunk * sizeof(int16_t));
    while (_preroll.count && _gemini.outboxSpace() > kOutboxReserve) {
        const size_t n = _preroll.pop(chunk, kChunk);
        if (!n) break;
        sendMic(chunk, n);
    }
    if (!_preroll.count) _prerollDraining = false;
}

void AgentController::beginUtterance(uint32_t now, float level, bool followUp) {
    _uttStart = now;
    _uttChunks = 0;
    _uttVoiced = 0;
    _uttPeak = level;
    _uttFollowUp = followUp;
    _uttTx0 = _gemini.txBytes();
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
    if (_prerollDraining) {  // cut off (the reply started / the mic closed): the rest is stale
        _prerollDraining = false;
        _preroll.clear();
    }
    _gemini.sendAudioStreamEnd();
    const float sentSec = _uttChunks * (AUDIO_MIC_CHUNK_MS / 1000.0f);
    const float voicedSec = _uttVoiced * (AUDIO_MIC_CHUNK_MS / 1000.0f);
    {  // what the uplink delivered meanwhile: 16 kHz PCM as base64 JSON needs ~45 KB/s
        const uint32_t ms = sinceMs(now, _uttStart);
        const uint32_t kbps = ms >= 1000 ? (_gemini.txBytes() - _uttTx0) / ms : 0;  // bytes/ms = KB/s
        if (_backlogDropped || (kbps && kbps < 40)) {
            Serial.printf("[Mic] อัปโหลดได้ %lu KB/s (เสียง 16 kHz ต้องการ ~45) - ทิ้งเสียงค้างส่ง %.1f วิ เพราะเน็ตส่งไม่ทัน\n",
                          (unsigned long)kbps, _backlogDropped / (float)AUDIO_UPLOAD_RATE);
        }
        _backlogDropped = 0;
    }
    if (_uttVoiced >= 3 && _audio->isPlaying()) {  // the answer is already playing: nothing to wait for
        Serial.printf("[Mic] จบประโยค: ส่ง %.1f วิ (เสียงพูด %.1f วิ) -> AI กำลังตอบ\n", sentSec, voicedSec);
    } else if (_uttVoiced >= 3) {  // real speech: from now on expect an answer
        Serial.printf("[Mic] จบประโยค: ส่ง %.1f วิ (เสียงพูด %.1f วิ, ดังสุด %.3f, ขยาย x%.1f) -> รอ AI ตอบ\n", sentSec,
                      voicedSec, _uttPeak, _agcGain);
        _replyWatchAt = now;
        _heardSinceSend = false;
        // stay awake for the answer: until audio / a transcript arrives (or checkNoReply gives up), the
        // radio rule ("idle 6 s -> sleep") must not close the session under Gemini
        _awaitingReply = true;
        noteActivity();
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

// Phase 3 (wiki 14): "Hi ESP" / "Jarvis" while the AI talks = like a tap on the face (the rest of this
// reply is dropped), then the mic streams at once - the command usually follows the word. The server
// cancels the old answer itself as soon as it hears the user.
void AgentController::bargeInNow(uint32_t now) {
    Serial.printf("[Mic] พูดแทรกด้วยคำปลุก (%s) - หยุดคำตอบ ฟังต่อ\n", _wakeWord.lastWord());
    _audio->clearPlayback();
    _dropAudio = true;
    _dropSince = now;
    flushAgentTurn(true);
    // like a wake by voice: the buffered word + whatever follows go out as the user's turn
    _wakeBuffering = true;
    _wakeHeard = true;
    _wakeVoiced = AGENT_WAKE_CHUNKS;
    _followUp.arm(now, AGENT_FOLLOWUP_MS);  // and keep streaming without waiting for an onset
    noteActivity();
    _face->setHint("หยุดพูดแล้ว ฟังอยู่", 1500);
}

void AgentController::checkNoReply(uint32_t now) {
    // T108: reasoning in the background (turnComplete IN_PROGRESS) without a later IDLE: stop waiting after 60 s
    if (_reasoningAt && sinceMs(now, _reasoningAt) > 60000) {
        _reasoningAt = 0;
        _awaitingReply = false;
        Serial.println("[Chat] still no IDLE 60 s after IN_PROGRESS - not waiting any longer");
    }
    // Google Search runs on the server and sends nothing until the answer: a session that offers it
    // and heard the question (transcript) waits longer before "AI ไม่ตอบ" - speaking again then would
    // cut off the answer that is on its way.
    const uint32_t limit = _searchInSetup && _heardSinceSend ? AGENT_NO_REPLY_SEARCH_MS : AGENT_NO_REPLY_WARN_MS;
    if (!_replyWatchAt || sinceMs(now, _replyWatchAt) < limit) return;
    _replyWatchAt = 0;
    _awaitingReply = false;  // no answer: let sleep / the radio take over again
    if (_heardSinceSend) {
        Serial.printf("[Mic] ⚠ AI ได้ยินแล้ว (มี transcript) แต่ไม่ตอบใน %u วิ - %s\n", (unsigned)(limit / 1000),
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
    // Power save (dim clock): the mic is off - only a tap wakes it (user's choice); asleep it stays on.
    // With the wake word only (wiki 14) it stays on there too: nothing but the word can wake it, and the
    // clock is where the board spends most of its time (the user called "Jarvis" at the clock: nothing)
    // T120: "both" (the default) too - only the word was left out; loudness still never wakes the clock
    const bool wordAtClock = _cfg.wakeMode != 0 && _wakeWord.ready();
    if (_wordTest && deadline::reached(now, _wordTestUntil)) {
        _wordTest = false;
        Serial.printf("[Wake] self-test over: heard %u time(s) in total\n", (unsigned)_wakeWord.detections());
    }
    const bool wordTest = _wordTest && _wakeWord.ready();
    // barge-in (phase 3, wiki 14): half duplex keeps the AI's own voice away from Gemini, but the wake word
    // may still hear the room through our echo removal - it is trained on one word, the AI's echo is not it
    const bool bargeIn = AGENT_BARGE_IN_WORD && AGENT_HALF_DUPLEX && speaking && !_sleeping && _configOk && !_muted &&
                         !_powerSave && _cfg.wakeMode != 0 && _wakeWord.ready();
    const bool allow = wordTest || bargeIn ||
                       (_configOk && !_muted && (!_powerSave || wordAtClock) && !(AGENT_HALF_DUPLEX && speaking));
    // the assistant just finished: users answer right away (not when it is about to sleep - e.g. after
    // play_radio, the follow-up window would stream the radio as "user speech")
    auto armFollowUp = [&]() {
        if (sinceMs(now, _lastAgentAudio) < 3000 && _gemini.isReady() && !_sleeping && !_sleepPending) {
            _followUp.arm(now, AGENT_FOLLOWUP_MS);
        }
    };
    if (allow != _gateAllowed) {
        _audio->setCaptureEnabled(allow);
        _gateAllowed = allow;
        if (!allow) {
            endUtterance(now);
            _onset = 0;
            if (!_wakeBuffering) _preroll.clear();
        } else {
            armFollowUp();
        }
    }
    // The reply's own edges. With the wake word on, the gate stays open while the AI talks (barge-in), so
    // the gate above never saw the reply: no follow-up window after any reply, a sentence still open when
    // the answer started ended only after it ("AI ไม่ตอบ" 8 s later) and a wake-up backlog still draining
    // took the 1.5 s recorded during the reply along as user speech (T069)
    const bool held = AGENT_HALF_DUPLEX && speaking && !_sleeping;
    if (held != _replyHeld) {
        _replyHeld = held;
        if (held) {  // the reply started: this sentence is over, its backlog is stale
            endUtterance(now);
            _onset = 0;
            if (!_wakeBuffering) _preroll.clear();
        } else {
            // what the barge-in buffer kept of the AI's own voice is not the user's - unless a barge-in took it
            if (!_wakeBuffering) _preroll.clear();
            if (allow && !_followUp.active(now)) armFollowUp();
        }
    }
    checkNoReply(now);
    if (_recActive && (int32_t)(now - _recDeadline) >= 0) finishRecording();
    if (millis() - now > 300) Serial.printf("[Mic] slow gate %lu ms\n", (unsigned long)(millis() - now));

    static int16_t* const chunk = (int16_t*)psramMalloc(kChunk * sizeof(int16_t));
    // what the speaker played during these very samples (DAC loopback)
    static int16_t* const refChunk = (int16_t*)psramMalloc(kChunk * sizeof(int16_t));
    // one chunk that takes > 300 ms says where (a steady 1.34 s "loop held (audioIn)" right after waking
    // over the radio, T065): the time to here, in the echo removal and in wake()
    struct SlowChunk {
        uint32_t t0 = micros(), read = 0, echo = 0, wake = 0;
        ~SlowChunk() {
            const uint32_t total = micros() - t0;
            if (total > 300000) {
                Serial.printf("[Mic] slow chunk %lu ms: read %lu, echo %lu, wake %lu, rest %lu ms\n",
                              (unsigned long)(total / 1000), (unsigned long)(read / 1000), (unsigned long)(echo / 1000),
                              (unsigned long)(wake / 1000), (unsigned long)((total - read - echo - wake) / 1000));
            }
        }
    };
    for (int iter = 0; iter < 6 && _audio->captureSamples() >= kChunk; iter++) {
        SlowChunk prof;
        float refLevel = 0.0f;
        _audio->readCapture(chunk, kChunk, refLevel, refChunk);
        prof.read = micros() - prof.t0;
        if (_pairRecActive) recordPairs(chunk, refChunk, kChunk);
        if (wordTest) {  // self-test: only the wake word gets this audio (the speaker included, no reference)
            _wakeWord.feed(chunk, nullptr, kChunk);
            continue;
        }
        // wake word (ESP-SR): the raw mic + reference, before our own echo removal changes the chunk.
        // Only while asleep - awake, the session listens itself (and the AFE would cost CPU for nothing)
        const bool feedWord = (_sleeping || bargeIn) && _cfg.wakeMode != 0 && _wakeWord.ready();
        if (feedWord && !_wordFeeding) _wakeWord.reset();  // no old audio: it starts listening now
        _wordFeeding = feedWord;
        if (feedWord && !_wakeWord.ownAec()) _wakeWord.feed(chunk, refChunk, kChunk);  // "MR": the AFE removes the echo
        // the reference is taken before the codec volume (measured: always 0.129 for the test tone, the
        // mic echo 0.002 at volume 30 and 0.31 at 90): a volume change scales the echo by the known gain
        const int vol = _audio->volume();
        if (vol != _wakeVolume) {
            if (_wakeVolume >= 0) {
                _radioSettle.arm(now, 2500);
                if (_wakeVolume > 0 && vol > 0) {  // 0.5 dB per codec step: carry what was learned over
                    const float g = powf(10.0f, (ES8311::volumeDb(vol) - ES8311::volumeDb(_wakeVolume)) / 20.0f);
                    _echoGate.scaleCoupling(g);
                    if (_aec) _aec->scale(g);
                } else {
                    _echoGate.relearn();
                    if (_aec) _aec->reset();
                }
            }
            _wakeVolume = vol;
        }
        // the mic gain (ES8311 ADC, 6 dB a step) scales the echo the same way: "set micgain 6" during a
        // test left the filter 2x off - it fell back to passing the mic through and the radio woke it
        const int mg = _audio->micGain();
        if (mg != _aecMicGain) {
            if (_aecMicGain >= 0) {
                _radioSettle.arm(now, 2500);
                const float g = powf(2.0f, (float)(mg - _aecMicGain));
                _echoGate.scaleCoupling(g);
                if (_aec) _aec->scale(g);
            }
            _aecMicGain = mg;
        }
        const float rawLevel = rms(chunk, kChunk);  // before the echo is removed (a saturated mic)
        // Echo removal (util/EchoCanceller.h): with the DAC loopback the speaker's sound is subtracted
        // from the mic - 27 dB measured with the radio. Everything below (wake, VAD, what Gemini hears)
        // then works on the room + the user only. Nothing playing = the mic passes through unchanged.
        const bool aecOn = _aec && _audio->hasEchoRef();
        if (aecOn) {
            const uint32_t t0 = micros();
            _aec->process(chunk, refChunk, chunk, kChunk, _radioSettle.active(now));
            const uint32_t us = micros() - t0;
            prof.echo = us;
            if (us > _aecMaxUs) _aecMaxUs = us;
            if (_aec->doubleTalk()) _aecDoubleTalk++;
        }
        // always the high-passed band with the canceller: nothing playing sends the mic out raw, and its
        // room hum (< 250 Hz) jumped the level over a noise floor learned on the radio's residual - the
        // radio stopping / changing station woke the assistant "by voice" every time
        const float level = aecOn ? (_aec->passthrough() ? _aec->micRms() : rms(chunk, kChunk)) : rawLevel;
        // "M": WakeNet hears what is left after our echo removal - it relearns as soon as a stream starts
        // (forced while settling), where the AFE's own AEC took about a minute of radio (T064)
        if (feedWord && _wakeWord.ownAec()) _wakeWord.feed(chunk, nullptr, kChunk);
        if (bargeIn) {  // the AI is talking: only the wake word gets this (no VAD, no noise floor, nothing to Gemini)
            // ...but keep the last 1.5 s (echo removed): WakeNet confirms the word ~0.5-1 s after it ends, and
            // "Hi ESP, ราคาทอง..." in one breath lost the whole command (T067). After a barge-in it all goes out.
            _preroll.push(chunk, kChunk);
            _preroll.trimTo(_wakeBuffering ? kWakeBufferSamples : AUDIO_UPLOAD_RATE * 3 / 2);
            if (_wakeWord.takeDetection()) {
                // the AI's own voice: TTS-trained "Jarvis" caught the leftover echo of the AI saying it (-63 dBFS,
                // and it says its name "จาวิส" often) - with two words only "Hi ESP" interrupts, and only a
                // voice clearly above that leftover counts (the user is not in the echo reference)
                const bool jarvis = strstr(_wakeWord.lastWord(), "jarvis") != nullptr;
                if ((jarvis && _wakeWord.twoWords()) || _wakeWord.lastDetectDb() < AGENT_BARGE_IN_MIN_DBFS) {
                    Serial.printf("[Mic] ไม่นับพูดแทรก: %s %.0f dBFS (น่าจะเป็นเสียง AI เอง)\n", _wakeWord.lastWord(),
                                  _wakeWord.lastDetectDb());
                } else {
                    bargeInNow(now);
                }
            }
            continue;
        }
        _threshold = _noise.threshold();
        // follows the background; a level that never pauses for 10 s is background too (a fan switched
        // on used to count as speech for ever). Not while the radio sounds without echo removal: the
        // echo estimate handles it then.
        // a chunk the canceller passed through raw / that clipped carries the speaker (radio), not the room:
        // it neither moves the noise floor nor counts as voice (at micgain 6 such chunks kept the mic open
        // and the radio was streamed to the AI for a minute)
        const bool echoUnreliable = aecOn && gRadio.active() && (_aec->bypassed() || _aec->clipped());
        const bool voice = !echoUnreliable && _noise.update(level, aecOn || !_audio->radioSounding());
        if (voice) {
            _lastVoice = now;
            noteActivity();
        }
        bool sent = false;

        if (_sleeping) {  // only listen for a wake-up
            _preroll.push(chunk, kChunk);
            _preroll.trimTo(AUDIO_UPLOAD_RATE * 3 / 2);  // 1.5 s before the wake-up (the start of the words)
            // Radio on: the speaker leaks into the mic. With the DAC loopback that echo is subtracted above
            // and the residual is judged like a quiet room. Without it (AUDIO_ECHO_REF 0) the level is
            // compared with the expected leak (util/EchoWakeGate.h), held twice as long. A tap always works.
            const bool radioOn = _audio->radioSounding();
            // a station change starts a new stream (silence, then full level): settle (learn) first -
            // a tap on the next station woke the assistant "by voice"
            if (radioOn && !_radioWasSounding) _radioSettle.arm(now, 2500);
            _radioWasSounding = radioOn;
            // the strict rules follow the radio SESSION: a stream that runs dry for a moment (seen on a
            // hotspot) used to drop to the plain voice threshold, and the room's own sound woke it
            const bool radioSession = gRadio.active();
            // the session ending ("radio stop", or the gap of a station change) left the last of the
            // radio in the room under the plain rule: it woke the assistant "by voice" every time
            if (!radioSession && _radioSessionWas) _radioSettle.arm(now, 1500);
            _radioSessionWas = radioSession;
            const bool settling = _radioSettle.active(now);
            bool loud;
            if (aecOn) {
                // a mic saturated by the speaker (volume 85+) carries no voice; never while still learning
                // a chunk passed through raw (filter off) is the radio itself: never a wake vote.
                // What is left of the radio grows with its echo (speaker/mic distortion, worse at a higher
                // mic gain: 0.17 -> 0.03 left at micgain 6), a fixed floor let it through - compare with the
                // echo just removed: music alone leaves 10-20 % of it, a voice is about as loud or louder
                const float echoLevel = _aec->echoRms();
                loud = voice && !settling && !(radioSession && (rawLevel > 0.35f || _aec->clipped())) && !_aec->bypassed() &&
                       (!radioSession || (level > AEC_WAKE_MIN_LEVEL && level > AEC_WAKE_ECHO_RATIO * echoLevel));
            } else {
                const float ref = _audio->radioOutLevel();
                loud = _echoGate.update(level, ref, _threshold, radioSession, settling, _noise.noise());
            }
            _wakeVotes = loud ? _wakeVotes + 1 : 0;
            _wakeHistory = (uint8_t)((_wakeHistory << 1) | (loud ? 1 : 0));  // last 8 chunks (320 ms)
            bool wakeNow;
            if (radioSession && aecOn) {  // speech has gaps between syllables: N of the last 8, not N in a row
                wakeNow = __builtin_popcount(_wakeHistory) >= AEC_WAKE_VOTES;
            } else {
                wakeNow = _wakeVotes >= (radioSession ? AGENT_WAKE_CHUNKS * 2 : AGENT_WAKE_CHUNKS);
            }
            // wake word mode: loudness alone no longer wakes it (the radio, the TV, a cough); "both" keeps
            // either. Without the model partition the setting falls back to loudness.
            // At the clock (power save) only the word wakes it in either mode (T120).
            if ((_cfg.wakeMode == 1 || _powerSave) && _wakeWord.ready()) wakeNow = false;
            const bool byWord = _wordFeeding && _wakeWord.takeDetection();
            if (byWord) {
                Serial.printf("[Mic] ปลุกด้วยคำปลุก (%s)%s\n", _wakeWord.lastWord(), radioSession ? " ขณะวิทยุเล่น" : "");
                wakeNow = true;
            } else if (wakeNow && radioSession) {  // why it woke over the radio (tune from real logs)
                if (aecOn) {
                    Serial.printf("[Mic] ปลุกด้วยเสียงขณะวิทยุเล่น: หลังตัดเสียงสะท้อน %.3f (ไมค์ %.3f, เสียงสะท้อนที่ตัดออก %.3f = x%.1f, เกณฑ์ %.3f)\n",
                                  level, rawLevel, _aec->echoRms(),
                                  _aec->echoRms() > 0 ? level / _aec->echoRms() : 0.0f, _threshold);
                } else {
                    const float expected = _echoGate.expected();
                    Serial.printf("[Mic] ปลุกด้วยเสียงขณะวิทยุเล่น: ระดับ %.3f, คาดว่าเสียงวิทยุรั่ว %.3f (x%.1f), เกณฑ์ %.3f\n",
                                  level, expected, expected > 0 ? level / expected : 0.0f, _threshold);
                }
            }
            if (wakeNow) {
                _wakeHistory = 0;
                const uint32_t tw = micros();
                wake(true);
                if (byWord) {
                    // T124: the word is not the request - sent along it came back as "chavés" / "¿Sabes?" / "Chaviz",
                    // and the model spent the turn on "ขอโทษค่ะ ไม่เข้าใจภาษาอื่น". Only what is said after it goes out
                    // (the detection fires at the end of the word); nothing said = like a tap
                    _preroll.clear();
                    _wakeHeard = false;
                    _wakeVoiced = 0;
                }
                _wokeByWord = byWord;
                prof.wake = micros() - tw;
            }
            recordChunk(chunk, kChunk, voice, false);
            continue;
        }
        if (!_gemini.isReady()) {  // connecting: keep what the user is saying
            _preroll.push(chunk, kChunk);
            _preroll.trimTo(_wakeBuffering ? kWakeBufferSamples : kPrerollSamples);
            if (voice) {
                _wakeHeard = true;
                if (_wakeBuffering && _wakeVoiced < 255) _wakeVoiced++;  // counted when the backlog is sent
            }
            recordChunk(chunk, kChunk, voice, false);
            continue;
        }
        if (_wakeBuffering) {
            if (_wakeHeard) {
                beginUtterance(now, level, false);
                sendPreroll();
            } else {  // tapped awake and nothing said yet: the normal onset decides
                _wakeBuffering = false;
                _preroll.trimTo(kPrerollSamples);
            }
        }

#if AGENT_LOCAL_VAD
        if (!_vadOpen) {
            _preroll.push(chunk, kChunk);
            _preroll.trimTo(kPrerollSamples);
            _onset = voice ? _onset + 1 : 0;
            const bool followUp = _followUp.active(now);
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
            if (_prerollDraining) {  // the wake-up backlog is still going out: this chunk goes behind it
                // An uplink slower than the audio (a phone hotspot: 26-48 KB/s for ~45 needed) never emptied
                // the backlog when every live chunk was appended: the mic streamed for 115 s, the radio stayed
                // ducked. Silence adds nothing to say: only voice (and short gaps inside words) goes behind it,
                // so the backlog drains and the words in it - usually the command - are kept.
                if (voice || sinceMs(now, _lastVoice) < 300) _preroll.push(chunk, kChunk);
                if (_preroll.count > kMaxBacklog) {  // still too far behind: the oldest audio goes first
                    _backlogDropped += _preroll.count - kMaxBacklog;
                    _preroll.trimTo(kMaxBacklog);
                }
                drainPreroll();
            } else {
                sendMic(chunk, kChunk);
            }
            sent = true;
            const bool followUp = _followUp.active(now);
            const bool silentFollowUp = _uttFollowUp && _uttVoiced == 0;
            // not while the backlog still goes out (audioStreamEnd would land before the words) - unless
            // the link cannot deliver it: 8 s after the hang-over (6 s of backlog at ~0.7x real time), the
            // rest is dropped
            const bool hangoverDone = sinceMs(now, _lastVoice) > AGENT_VAD_HANGOVER_MS;
            const bool backlogGivenUp = sinceMs(now, _lastVoice) > AGENT_VAD_HANGOVER_MS + 8000;
            if ((!_prerollDraining || backlogGivenUp) && (silentFollowUp ? !followUp : hangoverDone)) {
                endUtterance(now);
            }
            if (voice) _followUp.cancel();  // the user is speaking: the normal hang-over decides the end
        }
#else
        sendMic(chunk, kChunk);
        sent = true;
#endif
        recordChunk(chunk, kChunk, voice, sent);
    }
}

// ---- Diagnostic recording: exactly what the AGC would upload, 16 kHz mono, with VAD flags ----

// recref: raw mic + echo reference, interleaved 16 kHz, dumped as "RECPAIRS <rate> <bytes>" + PCM + END
// (measuring how much of the speaker's echo an adaptive filter removes - wiki 14)
void AgentController::recordPairs(const int16_t* mic, const int16_t* ref, size_t n) {
    for (size_t i = 0; i < n && _pairRecLen + 2 <= _pairRec.size(); i++) {
        _pairRec[_pairRecLen++] = mic[i];
        _pairRec[_pairRecLen++] = ref[i];
    }
    if (_pairRecLen + 2 > _pairRec.size()) {
        _pairRecActive = false;
        Serial.printf("RECPAIRS %u %u\n", (unsigned)AUDIO_UPLOAD_RATE, (unsigned)(_pairRecLen * 2));
        Serial.write((const uint8_t*)_pairRec.data(), _pairRecLen * 2);
        Serial.print("\nEND\n");
        _pairRec.clear();
        _pairRec.shrink_to_fit();
    }
}

void AgentController::startRecording(uint8_t sec) {
    if (_recRequestPairs) {
        if (sec < 1) sec = 1;
        if (sec > 20) sec = 20;
        _pairRec.assign((size_t)sec * AUDIO_UPLOAD_RATE * 2, 0);
        _pairRecLen = 0;
        _pairRecActive = true;
        Serial.printf("[Mic] อัดไมค์ + เสียงอ้างอิง %u วิ (RECPAIRS)\n", (unsigned)sec);
        return;
    }
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
    info += String(_noise.noise(), 4);
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
