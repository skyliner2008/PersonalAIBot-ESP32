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
    // Google Search runs on the server and sends nothing until the answer: a session that offers it
    // and heard the question (transcript) waits longer before "AI ไม่ตอบ" - speaking again then would
    // cut off the answer that is on its way.
    const uint32_t limit = _searchInSetup && _heardSinceSend ? AGENT_NO_REPLY_SEARCH_MS : AGENT_NO_REPLY_WARN_MS;
    if (!_replyWatchAt || sinceMs(now, _replyWatchAt) < limit) return;
    _replyWatchAt = 0;
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
