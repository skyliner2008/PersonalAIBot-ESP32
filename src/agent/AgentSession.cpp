/**
 * @file AgentSession.cpp
 * @brief AgentController: Wi-Fi, Live session, model fallback, tool calls, web-tool results
 * @see wiki/04-System-Architecture.md
 */

#include "AgentInternal.h"

#include <esp_wifi.h>  // esp_wifi_scan_stop

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
        _wifiScanning = false;
        wifiBegin(_cfg.wifiSsid, _cfg.wifiPass);
        _wifiStarted = true;
    }
    if (gWifiJoinPrimaryNow.exchange(false)) {  // the menu: the configured network was tapped (T102)
        Serial.printf("[WiFi] Joining %s now (menu)\n", _cfg.wifiSsid);
        if (_wifiScanning) esp_wifi_scan_stop();
        _wifiScanning = false;
        _wifiFailures = 0;
        _wifiRetryMs = kWifiRetryMinMs;
        if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();
        wifiBegin(_cfg.wifiSsid, _cfg.wifiPass);
    }
    if (_wifiScanning) {  // looking for a remembered network (or, connected, for the configured one again)
        const int16_t found = WiFi.scanComplete();
        if (found != WIFI_SCAN_RUNNING || now - _wifiScanAt >= kWifiScanTimeoutMs) {
            if (found == WIFI_SCAN_RUNNING) esp_wifi_scan_stop();  // hung: stop it, or no scan can start again
            wifiScanDone(found);
        } else if (!_wifiUp) {
            return;  // not connected: nothing else until the scan answers
        }
    }
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up != _wifiUp) {
        _wifiUp = up;
        if (up) {
            Serial.printf("[WiFi] Connected to %s, IP %s, RSSI %d dBm\n", _wifiTarget,
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            if (!strcmp(_wifiTarget, _cfg.wifiSsid)) gWifiPrimaryReason = 0;
            if (gSettings.rememberWifi(_wifiTarget, _wifiTargetPass)) {  // T099: joined before = joined again
                Serial.printf("[WiFi] Remembered %s (%u networks)\n", _wifiTarget,
                              (unsigned)gSettings.knownWifiCount());
            }
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
    // signed: wifiBegin() stamps millis(), a few ms after `now` - unsigned, that read as "4 billion ms ago" and
    // retried at once (a 2nd begin right after a network change, board log T102)
    if (!up && (int32_t)(now - _wifiBeginAt) > (int32_t)_wifiRetryMs) {
        _wifiFailures++;
        if (!strcmp(_wifiTarget, _cfg.wifiSsid)) gWifiPrimaryReason = gWifiReason.load();  // for the menu (T102)
        if (_wifiFailures >= 3 && !_holdUntil && !_fatalError) {  // tell the user why
            char buf[160];
            snprintf(buf, sizeof(buf), "ต่อ Wi-Fi \"%s\" ไม่ได้: %s (รหัส %u)", _wifiTarget,
                     wifiReasonTh(gWifiReason.load()), (unsigned)gWifiReason.load());
            _face->setSystemText(buf, 0);
            _wifiErrorShown = true;
        }
        Serial.printf("[WiFi] Retry %u in %lu s (last reason %u)\n", (unsigned)_wifiFailures,
                      (unsigned long)(_wifiRetryMs / 1000), (unsigned)gWifiReason.load());
        WiFi.disconnect();
        _wifiRetryMs = _wifiRetryMs * 2 > kWifiRetryMaxMs ? kWifiRetryMaxMs : _wifiRetryMs * 2;
        // T099: every 3rd failure, look for a remembered network in range (moved house, phone hotspot, ...)
        // unless the settings menu is scanning itself. Not before the 3rd (~21 s with the back-off): a network
        // just picked in the menu gets its chance before an old remembered one takes over (T102)
        if (_wifiFailures % 3 == 0 && gSettings.knownWifiCount() > 0 && wifiStartScan(now)) {
            Serial.println("[WiFi] Looking for a remembered network...");
            return;
        }
        wifiBegin(_wifiTarget, _wifiTargetPass);
    }
    // T099: on a remembered network (hotspot, ...) look for the configured one now and then and go back to it -
    // only while asleep: the switch drops the connection, never in the middle of a conversation
    // T103: connected, look for a remembered network with a much stronger signal (the phone hotspot back next to
    // the board) and move to it
    // (not during a download: the radio leaves the channel for the scan - it lined up with both stuck downloads, T104)
    if (up && _sleeping && !_wifiScanning && !_web.busy() && now - _wifiScanAt >= kWifiBetterCheckMs &&
        (gSettings.knownWifiCount() >= 2 || strcmp(_wifiTarget, _cfg.wifiSsid)) && wifiStartScan(now)) {
        Serial.printf("[WiFi] On %s (%d dBm) - looking for a stronger remembered network\n", _wifiTarget, WiFi.RSSI());
    }

    if (!_timeValidSeen && nowEpoch()) {
        _timeValidSeen = true;
        char buf[24];
        timefmt::formatDateTime(nowEpoch(), TIME_UTC_OFFSET_SEC, buf, sizeof(buf));
        Serial.printf("[Time] Synced: %s (UTC+7)\n", buf);
        if (_storage->kind() != StorageKind::None) {
            _episodes.reloadDays(nowEpoch());  // keeps what was said before the sync (open() dropped it)
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
    // (a firmware download keeps the session closed: the link, the RAM and no wake-up mid-install)
    const bool wanted = up && _configOk && !_sleeping && !_fatalError && !_holdUntil && !gOta.installing();
    if (wanted && !_gemini.started()) _gemini.start();

    if (_reconnectPending && !_audio->isPlaying() && _gemini.started()) {
        _reconnectPending = false;
        _gemini.restart();  // resumes with the handle
    }
}

/** An async scan for wifiScanDone(); false = one is already running (the settings menu) or it failed. */
bool AgentController::wifiStartScan(uint32_t now) {
    _wifiScanAt = now;  // also spaces out the next try when this one cannot start
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING || WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) return false;
    _wifiScanning = true;
    return true;
}

void AgentController::wifiBegin(const char* ssid, const char* pass) {
    if (ssid != _wifiTarget) {  // a retry passes the current target itself
        strncpy(_wifiTarget, ssid, sizeof(_wifiTarget) - 1);
        _wifiTarget[sizeof(_wifiTarget) - 1] = '\0';
        strncpy(_wifiTargetPass, pass, sizeof(_wifiTargetPass) - 1);
        _wifiTargetPass[sizeof(_wifiTargetPass) - 1] = '\0';
    }
    WiFi.begin(_wifiTarget, _wifiTargetPass);
    _wifiBeginAt = millis();
    Serial.printf("[WiFi] Connecting to %s...\n", _wifiTarget);
}

/**
 * The scan for remembered networks finished (T099). The configured network wins whenever it is in range;
 * otherwise the best remembered one other than the network that just failed; nothing in range = the
 * configured one again. Connected (the 2-minute check while asleep, T103): move to a remembered network at
 * least kWifiSwitchMarginDb stronger than the current one, else stay.
 */
void AgentController::wifiScanDone(int16_t found) {
    _wifiScanning = false;
    std::vector<String> names;
    std::vector<wifiknown::Seen> seen;
    bool primaryInRange = false;
    for (int i = 0; i < found; i++) names.push_back(WiFi.SSID(i));
    for (int i = 0; i < found; i++) {
        if (!names[i].length()) continue;
        seen.push_back({names[i].c_str(), (int)WiFi.RSSI(i)});
        primaryInRange |= names[i] == _cfg.wifiSsid;
    }
    // the results are left for the next scan to clear: the settings menu may be reading the same scan
    const bool onPrimary = !strcmp(_wifiTarget, _cfg.wifiSsid);
    if (WiFi.status() == WL_CONNECTED) {
        int current = WiFi.RSSI();
        for (const wifiknown::Seen& s : seen) {
            if (!strcmp(s.ssid, _wifiTarget)) current = s.rssi;  // from the same scan as the others: comparable
        }
        wifiknown::List known = gSettings.knownWifi();
        const int better = known.stronger(seen.data(), seen.size(), _wifiTarget, current, kWifiSwitchMarginDb);
        if (better >= 0) {
            const wifiknown::Net& n = known.at((size_t)better);
            Serial.printf("[WiFi] %s is %d+ dB stronger than %s (%d dBm) - switching\n", n.ssid, kWifiSwitchMarginDb,
                          _wifiTarget, current);
            WiFi.disconnect();
            _wifiRetryMs = kWifiRetryMinMs;
            _wifiFailures = 0;
            wifiBegin(n.ssid, n.pass);
        } else if (!onPrimary && primaryInRange && known.find(_cfg.wifiSsid) < 0) {
            // the configured network was never joined (not remembered, nothing to compare): back to it as before
            Serial.printf("[WiFi] %s is in range again\n", _cfg.wifiSsid);
            WiFi.disconnect();
            wifiBegin(_cfg.wifiSsid, _cfg.wifiPass);
        }
        return;
    }
    if (!onPrimary && primaryInRange) {
        Serial.printf("[WiFi] %s is in range again\n", _cfg.wifiSsid);
        wifiBegin(_cfg.wifiSsid, _cfg.wifiPass);
        return;
    }
    wifiknown::List known = gSettings.knownWifi();
    known.forget(_wifiTarget);  // it just failed
    const int pick = known.pick(seen.data(), seen.size());
    if (pick < 0) {
        String names;  // which ones the scan saw (a remembered network missing here = not heard in this scan)
        for (const wifiknown::Seen& s : seen) names += String(names.length() ? ", " : "") + s.ssid + " " + s.rssi;
        Serial.printf("[WiFi] No remembered network in range (%d seen: %s)\n", (int)found, names.c_str());
        wifiBegin(_cfg.wifiSsid, _cfg.wifiPass);
        return;
    }
    const wifiknown::Net& n = known.at((size_t)pick);
    Serial.printf("[WiFi] %s not reachable - trying remembered network %s\n", _wifiTarget, n.ssid);
    char buf[160];
    snprintf(buf, sizeof(buf), "ต่อ \"%s\" ไม่ได้ — ลองใช้ Wi-Fi ที่เคยต่อ \"%s\"", _wifiTarget, n.ssid);
    _face->setSystemText(buf, 8000);
    _wifiRetryMs = kWifiRetryMinMs;
    wifiBegin(n.ssid, n.pass);
}

void AgentController::startFreshSession(const char* hint) {
    flushUserTurn();
    flushAgentTurn(false);
    _resumeHandle = "";
    _freshPending = true;  // handles still queued from the old session must not bring it back
    _audio->clearPlayback();
    _dropAudio = false;
    _awaitingReply = false;
    _awaitToolAnswer = false;
    _reasoningAt = 0;
    _nudgeAt = 0;
    if (hint) _face->setHint(hint, 2000);
    if (_gemini.started()) _gemini.restart();
}

void AgentController::buildSystemPrompt(PsString& p) {
    const persona::Personality& pers = persona::kPersonalities[_cfg.personality % persona::kPersonalityCount];
    const persona::VoiceInfo* vi = persona::findVoice(_cfg.voice);
    const char* voiceName = vi ? vi->name : (_cfg.voice[0] ? _cfg.voice : "Aoede");
    const persona::Gender gender = (persona::Gender)_cfg.gender;
    p.reserve(5600);
    p = "คุณคือ \"";
    p += _cfg.agentName;
    p += "\" ผู้ช่วยส่วนตัว AI บนอุปกรณ์ตั้งโต๊ะของผู้ใช้";
    if (_cfg.userName[0]) {  // T110: "ผู้ใช้ชื่อ" alone - the model never used it
        p += " เรียกผู้ใช้ว่า \"";
        p += _cfg.userName;
        p += "\" (บ้างเป็นครั้งคราว ไม่ต้องทุกประโยค)";
    }
    p += "\nโปรไฟล์เสียงและตัวตนที่ล็อกไว้ตามการตั้งค่าของผู้ใช้ (STRICT — ห้ามเปลี่ยนเองเด็ดขาด):"
         "\n- เสียงพูด (Voice Profile): ";
    p += voiceName;
    if (vi) {
        p += " (โทนเสียง";
        p += vi->gender == persona::kFemale ? "ผู้หญิง" : "ผู้ชาย";
        p += " • ";
        p += vi->styleTh;
        p += ")";
    }
    p += "\n- เพศและคำลงท้าย: ";
    p += persona::genderNameTh(gender);
    p += " — ";
    p += persona::politeParticle(gender);
    if (gender == persona::kFemale) {
        p += " (ต้องใช้คำลงท้าย \"ค่ะ/คะ\" และรักษาโทนเสียงผู้หญิงตามโปรไฟล์เสียง ";
        p += voiceName;
        p += " เสมอ ห้ามพูด \"ครับ\" หรือเปลี่ยนเป็นโทนเสียงผู้ชายเด็ดขาด แม้ชื่อผู้ช่วยจะเป็น \"";
        p += _cfg.agentName;
        p += "\" ก็ตาม)";
    } else if (gender == persona::kMale) {
        p += " (ต้องใช้คำลงท้าย \"ครับ\" และรักษาโทนเสียงผู้ชายตามโปรไฟล์เสียง ";
        p += voiceName;
        p += " เสมอ ห้ามพูด \"ค่ะ/คะ\" หรือเปลี่ยนเป็นโทนเสียงผู้หญิงเด็ดขาด)";
    }
    p += "\n- นิสัย (โปรไฟล์ ";
    p += pers.nameTh;
    p += "): ";
    p += pers.prompt;
    p += "\n- กฎล็อกเนื้อเสียงและสำเนียง (VOICE LOCK — สำคัญที่สุด): ต้องรักษาเนื้อเสียง (Timbre) ระดับความทุ้ม-แหลม (Pitch) "
         "จังหวะความเร็ว และเอกลักษณ์น้ำเสียงของโปรไฟล์ \"";
    p += voiceName;
    if (vi) {
        p += " (";
        p += vi->styleTh;
        p += ")";
    }
    p += "\" ให้คงที่สม่ำเสมอเหมือนเป็นคนคนเดียวกันพูดทุกประโยคตั้งแต่ต้นจนจบเซสชัน "
         "รักษาความเร็วและจังหวะการพูด (Speaking Tempo) ให้สบายๆ พอดีๆ เว้นวรรคหายใจระหว่างประโยคและระหว่างหัวข้ออย่างสม่ำเสมอเท่ากันตั้งแต่ประโยคแรกจนถึงประโยคสุดท้าย "
         "ห้ามเร่งความเร็วการพูดรัวขึ้นในช่วงกลางหรือช่วงท้ายของคำตอบยาวๆ เด็ดขาด "
         "ห้ามดัดเสียง ห้ามบีบเสียงแหลมหรือกดเสียงทุ้ม ห้ามเปลี่ยนโทนเสียงไปมาตามอารมณ์ของเรื่องหรือหลังเรียกใช้ tool "
         "ใช้สำเนียงภาษาไทยมาตรฐานที่ชัดถ้อยชัดคำ เป็นธรรมชาติ ห้ามพูดติดสำเนียงต่างชาติ/สำเนียงฝรั่ง ห้ามดัดลิ้น "
         "(การแสดงอารมณ์หรือความขี้เล่นตามนิสัยให้สื่อผ่านการเลือกใช้คำพูดเท่านั้น ห้ามเปลี่ยนเนื้อเสียงหรือเร่งจังหวะการพูด)";
    // waking it = calling its name: "จาวิส" alone was taken for a question (search_knowledge "jawit") or
    // replayed an old request (it opened the radio)
    // T124 (board logs): "Jarvis" came back as "Chaves", "¿Ya ves?", "¿Sabes?", "Jawez" - 3.8 then followed the Thai lock
    // below ("heard another language: ask again in Thai") instead of answering its name; 3.1 answered it
    p += "\nการเรียกชื่อ: ผู้ใช้ปลุกคุณด้วยการเรียกชื่อ (เสียงอาจถอดเป็นคำที่ออกเสียงใกล้เคียง เช่น Jarvis, jawit, จาวิส, "
         "Chaves, Chavez, Jawez, ¿Sabes?, ¿Ya ves? — คำสั้นๆ แบบนี้คือการเรียกชื่อคุณเป็นภาษาไทย ไม่ใช่ภาษาอื่น "
         "ห้ามขอให้พูดภาษาไทย ให้ขานรับแทน: กฎนี้มาก่อนกฎเรื่องภาษา)";
    if (_cfg.wakeMode != 0) {  // the offline wake word reaches the session too ("Hi ESP" changed the radio station)
        if (_cfg.wakeWord != 0) p += " หรือคำปลุก \"Hi ESP\" (อาจถอดเป็น ไฮอีเอสพี, Hi ISP ฯลฯ)";
        if (_cfg.wakeWord != 1) p += " หรือคำปลุก \"Jarvis\"";
    }
    p += " ถ้าประโยคมีแค่ชื่อคุณ คำปลุก หรือคำทักทาย ให้ขานรับสั้นๆ อย่างเป็นกันเองด้วยน้ำเสียงโปรไฟล์เดิม (ไม่ซ้ำคำเดิมทุกครั้ง) แล้วรอฟัง ห้ามเรียก tool ห้ามค้นหาคำนั้น "
         "ห้ามเปิด/เปลี่ยนวิทยุ และห้ามทำคำสั่งเก่า ถ้ามีคำสั่งต่อท้าย ให้ทำตามคำสั่งนั้น";
    // what it can do changes with the firmware and what is connected: read it, never from memory (about_me)
    p += "\nตัวตน: ถ้าผู้ใช้ถามว่าคุณเป็นใคร ทำอะไรได้บ้าง ขอให้แนะนำ/นำเสนอ/รีวิวตัวเอง หรือถามว่าทำสิ่งใดได้ไหม "
         "ให้เรียก about_me แล้วตอบจากผลนั้นเท่านั้น (ความสามารถเพิ่มขึ้นเมื่ออัปเดตเฟิร์มแวร์ ห้ามตอบจากความจำ) "
         "การแนะนำ/นำเสนอ/รีวิวตัวเอง: เล่าให้ครบทุกหมวดตาม how_to_present ด้วยจังหวะการพูดที่สบายๆ สม่ำเสมอ เว้นวรรคหายใจระหว่างหมวด ห้ามเร่งสปีดการพูดในช่วงท้าย";
    // T110: "สั้น" alone gave flat one-line read-outs ("lifeless" next to the phone app's Jarvis). T111 (the user): the
    // voice is the point - nobody should have to read the screen - so answers are complete and exact, long when needed
    // T116 (the user: it neither spoke nor understood Thai): with "the user's language" alone, mic audio that comes
    // through broken can be taken for another language and the reply follows it (a quiet 0.2 s was heard as Spanish):
    // the language is a setting, locked to Thai unless the user picks otherwise
    switch ((AiLanguage)_cfg.language) {
        case AiLanguage::English:
            p += "\nการพูด: ภาษาถูกล็อกเป็นภาษาอังกฤษในการตั้งค่า: speak English only, whatever language you hear; "
                 "if the user is unclear, ask again in English, never guess · ";
            break;
        case AiLanguage::Auto:
            p += "\nการพูด: ตอบเป็นภาษาเดียวกับที่ผู้ใช้พูด (ปกติภาษาไทย) ถ้าฟังไม่ชัดให้ขอให้พูดอีกครั้ง ห้ามเดา · ";
            break;
        default:
            p += "\nการพูด: ภาษาถูกล็อกเป็นภาษาไทยในการตั้งค่า: ผู้ใช้พูดไทยเสมอ ฟังเสียงเป็นภาษาไทย และตอบเป็นภาษาไทยเท่านั้น "
                 "(ภาษาอื่นเฉพาะเมื่อผู้ใช้ขอให้แปลหรือสอนคำ) ถ้าฟังไม่ชัดหรือฟังเป็นภาษาอื่น ให้ขอให้พูดอีกครั้งเป็นภาษาไทย ห้ามเดา · ";
            break;
    }
    p += "เสียงของคุณคือช่องทางหลัก ผู้ใช้ไม่ต้องอ่านจอ (จอแสดงประกอบเท่านั้น) "
         "จึงตอบให้ละเอียด ถูกต้อง เข้าใจง่าย ชัดเจน: ตัวเลขพูดตรงตามผลของ tool (ทศนิยมได้ถึง 2 หลัก หน่วยเป็นคำไทย เช่น ดอลลาร์ บาท เปอร์เซ็นต์) "
         "ไม่ปัดจนคลาดเคลื่อน · ยาวตามเนื้อหา: คุยเล่น/รับคำสั่ง 1-2 ประโยค ตอบข้อมูลให้ครบทุกส่วนที่ผู้ใช้ควรรู้ พร้อมบอกว่าแปลว่าอะไร "
         "(ขึ้นหรือลง ดีหรือน่าห่วง ควรทำอะไร) เรื่องที่มีรายละเอียดมาก (ข่าวหลายข่าว ภาพรวมตลาด วิเคราะห์ ระดับน้ำหลายจุด) ตอบยาวได้ "
         "เล่าไล่ทีละเรื่องจนครบ · พูดอย่างเป็นธรรมชาติตามนิสัยที่ตั้งไว้โดยคงเนื้อเสียงและโทนเสียงโปรไฟล์ ";
    p += voiceName;
    p += " ให้เหมือนเดิมทุกครั้ง ใช้คำหลากหลาย ไม่ขึ้นต้นแบบเดิมซ้ำๆ ";
    p += persona::politeParticle(gender);
    p += " แปลงตาราง/รายการเป็นประโยคพูด ไม่อ่านสัญลักษณ์ ห้ามพูดชื่อ tool หรือคำสั่งแบบ ชื่อ(...) / ชื่อ{...} ออกมา (เรียก tool อย่างเงียบๆ) "
         "ห้ามใส่ tag HTML / markdown เช่น <div> (เป็นเสียงพูด ไม่ใช่หน้าเว็บ)\n";  // T138: "set_expression{...}<div>"
    p += "การทำงานแบบเอเจนต์: คำขอชัดเจนให้ลงมือเรียก tool ทันทีใน turn นั้น ไม่ต้องถามยืนยันเรื่องเล็ก ห้ามพูดว่าเดี๋ยวดูให้แล้วไม่เรียก "
         "• งานหลายขั้นให้เรียกหลาย tool ต่อกันเองจนได้คำตอบครบ (เช่น ดึงราคาแล้ว calculate) • ถามหลายเรื่องในครั้งเดียว ตอบให้ครบทุกเรื่อง "
         "• tool ไม่ได้ผล ลอง tool อื่นหรือคำค้นอื่นก่อนบอกว่าทำไม่ได้ • คิดล่วงหน้าแทนผู้ใช้: ถ้ามีขั้นต่อไปที่ช่วยได้ชัดๆ "
         "(ตั้งเตือน แจ้งเตือนราคา จดงาน ดูต่อ) เสนอสั้นๆ 1 ประโยคท้ายคำตอบ (ไม่ต้องทุกครั้ง และไม่ทำเองถ้าไม่ได้ขอ)\n";
    if (_activeModel != (uint8_t)LiveModel::FlashLive31) {
        // T108: 3.8 Live / Extended Thinking have proactive audio on for good (cannot be turned off): it may stay
        // silent on audio it judges not meant for it - the board only sends what its own VAD / wake word let through
        p += "การฟัง: เสียงที่ส่งมาถึงคุณผ่านการกรองของอุปกรณ์แล้ว ถ้าเป็นผู้ใช้พูดกับคุณ (คำถาม/คำสั่ง แม้สั้นหรือไม่ได้เรียกชื่อ) "
             "ให้ตอบเสมอ เงียบเฉพาะเสียงที่ชัดว่าไม่ได้พูดกับคุณ (วิทยุ ทีวี คนอื่นคุยกัน)\n";
    }
    if (behaviorFor(_activeModel) != ToolBehavior::Unspecified) {
        // T108: on Extended Thinking every tool runs in the background (NON_BLOCKING only); T110: on 3.8 Live the
        // Internet lookups do (AgentTools::isBackgroundTool) - the model keeps talking meanwhile
        p += _activeModel == (uint8_t)LiveModel::Live38Thinking ? "เครื่องมือทุกตัว" : "เครื่องมือดึงข้อมูลจากเน็ต (อากาศ ข่าว ราคา น้ำ ความรู้ ฯลฯ)";
        p += " ทำงานเบื้องหลังหลายวินาที: ตอนเรียกให้พูดรับคำสั้นๆ อย่างเป็นธรรมชาติ (เช่น ขอเช็คให้แป๊บนึง) แล้วหยุดรอ "
             "ผลมาถึงแล้วค่อยเล่า ห้ามพูดตัวเลขหรือข้อเท็จจริงก่อนได้ผล\n";
    }
    // T140: no "เรียก set_expression" any more - the board moves the eyes from the words said (EmotionCue)
    p += "ดวงตา: ดวงตาบนจอแสดงอารมณ์ตามคำที่คุณพูดเอง ไม่ต้องเรียก tool ใดๆ\n"
         "ความจำ: เรียก save_memory/save_relation ทันทีที่รู้ข้อมูลสำคัญของผู้ใช้ และ recall_memory "
         "ก่อนตอบเรื่องส่วนตัว ห้ามแต่งความจำ บันทึกเฉพาะสิ่งที่ผู้ใช้พูดถึงตัวเองชัดเจน "
         "(ไม่ใช่เสียงไม่ชัด เสียงวิทยุ/ทีวี หรือคนอื่นคุยกัน)\n"
         "ผู้ช่วยส่วนตัว: ใช้ set_timer, set_reminder, add_task ฯลฯ ตามที่ผู้ใช้ขอ ";
    p += "ข้อมูลสด (แสดงกราฟิกบนจอประกอบให้เอง แล้วคุณเล่าจากผลของ tool นั้นเท่านั้นให้ครบถ้วน ถ้า tool ตอบ error/busy "
         "หรือยังไม่ตอบ ให้บอกตรงๆ ว่ายังไม่ได้ข้อมูล ห้ามเดาหรือใช้ตัวเลขเก่า): อากาศ/ฝน/ฝุ่น PM2.5 = get_weather, "
         "ราคาน้ำมันในไทย = get_oil_price, หุ้น/ดัชนี/ทองคำ/คริปโต/ค่าเงิน/น้ำมันดิบ = get_market_quote "
         "(ทองคำได้ราคาทองแท่งไทยบาทละโดยประมาณมาด้วย: คนไทยถามราคาทองมักหมายถึงอันนี้ เล่าคู่กับทองโลก), "
         "แลกเงิน = convert_currency, ข่าว = get_news, คิดเลข = calculate (ห้ามคิดเลขเอง), แปลงหน่วย = convert_units, "
         "เวลาต่างประเทศ = get_world_time, ความรู้ทั่วไป/ใครคือ/คืออะไร = search_knowledge, จดโน้ตยาว = notes ";
    if (_cfg.waterTools) {  // T100: measured data, while get_weather is the forecast
        p += "น้ำ (วัดจริงจากคลังข้อมูลน้ำแห่งชาติ): ระดับน้ำ/น้ำท่วม/น้ำล้นตลิ่ง = water level, ฝนตกไปเท่าไร = water rain (days 1/3/5/7 ตามที่ถาม ไม่ระบุ = 1), "
             "พายุ = water storm · ใส่ province เมื่อถามถึงจังหวัด/สถานที่ · ทั่วประเทศ/จังหวัดไหนหนักสุด = ไม่ใส่ province "
             "(ได้ 8 จังหวัดสูงสุด, ใช้เวลาราว 10 วิ: บอกผู้ใช้ว่ากำลังดึงข้อมูลทั้งประเทศ) ";
    }
    if (_cfg.financeTools) {
        p += "การเงิน: ภาพรวมตลาด = market_overview, สัญญาณเทคนิคเร็วๆ = market_analysis, "
             // T142: the candle history - levels and trend from real bars, a chart on the screen
             "กราฟ/แนวรับแนวต้าน/แนวโน้มจากแท่งเทียน/ความสัมพันธ์ระหว่างสินทรัพย์ (compare) = market_chart, หุ้นเด่น/ร่วง = market_scan, "
             "ข่าวเศรษฐกิจ = economic_calendar, ขนาด lot = position_size, แจ้งเตือนราคา = price_alert · "
             // T139 (the user): "อย่างไรก็ตาม ข้อมูลนี้ไม่ใช่คำแนะนำการลงทุน" closed every finance answer - not wanted
             "ห้ามปิดท้ายด้วยคำเตือนว่าไม่ใช่คำแนะนำการลงทุน แต่ปิดท้ายด้วยข้อเสนอที่ช่วยต่อได้จริง 1 อย่างที่เข้ากับสิ่งที่ถาม "
             "เช่น ตั้งแจ้งเตือนเมื่อราคาถึงระดับสำคัญ (บอกระดับจากข้อมูล), ดูวิเคราะห์เทคนิค/หลายกรอบเวลา, เทียบกับสินทรัพย์ที่เกี่ยวข้อง, "
             "ดูข่าวเศรษฐกิจที่อาจกระทบ หรือคำนวณขนาด lot · สลับข้อเสนอไม่ซ้ำเดิมทุกครั้ง · ถ้าผู้ใช้ถามว่าควรซื้อ/ขายไหม "
             "ให้วิเคราะห์จากข้อมูลจริง (แนวโน้ม ตัวชี้วัด แนวรับ-แนวต้าน) แล้วสรุปมุมมองตรงๆ ";
    }
    p += searchAvailable() ? "เรื่องอื่นที่ต้องใช้ข้อมูลล่าสุดใช้ Google Search "
                           : "ไม่มีการค้นเว็บทั่วไป: เรื่องล่าสุด (ผลกีฬา หวย เหตุการณ์ ราคาทองตามประกาศสมาคมฯ) ใช้ get_news ระบุ topic "
                             "แล้วตอบจากหัวข่าวพร้อมบอกวันที่ ข้อเท็จจริงทั่วไปใช้ search_knowledge ถ้ายังหาไม่ได้ให้บอกตรงๆ "
                             "ว่าค้นไม่ได้ ห้ามเดา ";
    p += "ห้ามเรียก configure_assistant เพื่อเปลี่ยนเสียง/นิสัย/ชื่อเองเด็ดขาด (โปรไฟล์ถูกล็อกตามที่ผู้ใช้ตั้งค่าไว้ เรียกได้เฉพาะเมื่อผู้ใช้พูดสั่งให้เปลี่ยนเสียง/นิสัย/ชื่ออย่างชัดเจนเท่านั้น) ถ้าผู้ใช้บอกลาหรือราตรีสวัสดิ์ให้เรียก go_to_sleep\n"
         "ควบคุมเครื่อง: เสียงดัง/เบา = set_volume (change +/-, ปรับเฉพาะวิทยุ เสียงพูดของคุณล็อกไว้ที่เดิม), จอสว่าง/มืด = set_brightness (change +/-), "
         "ความไวไมค์ / ตั้งเวลาพัก / ตั้งเวลาโหมดนาฬิกา = configure_device, พักหน้าจอ / โหมดนาฬิกาเดี๋ยวนี้ = rest_screen, วิทยุ = play_radio / stop_radio (เปิดแล้วพูดชื่อสถานีสั้นๆ แล้วเงียบ เพลงที่เล่นอยู่ดูได้จาก get_device_status), ปิดไมค์ = mute_microphone "
         "(เปิดไมค์ด้วยเสียงไม่ได้ บอกผู้ใช้ให้แตะปุ่มไมค์บนจอ)\n"
         "ข้อความที่ขึ้นต้นด้วย [แจ้งเตือนจากระบบ] หรือ [แจ้งเตือนราคาจากระบบ] มาจากอุปกรณ์ ให้แจ้งผู้ใช้ทันทีสั้นๆ\n";
    if (gPeriph.count()) {  // the user's own hardware (agent/devices.json, wiki 15)
        String devices;
        gPeriph.summary(devices);
        p += "อุปกรณ์ที่ต่อกับบอร์ด (สั่ง/อ่านด้วย tool device, เรียกตามชื่อนี้): ";
        p += devices.c_str();
        p += " • switch = เปิด/ปิด, dimmer = 0-100, input/analog = อ่านค่าอย่างเดียว ถามสถานะให้ read ก่อนตอบ ห้ามเดา\n";
    }
    if (gStations.count()) {  // play_radio picks from this list (number, name or genre)
        String list;
        gStations.describe(list);
        p += "สถานีวิทยุในรายการ: ";
        p += list.c_str();
        RadioStation last;
        const int li = gStations.lastIndex();
        if (li >= 0 && gStations.get((size_t)li, last)) {  // "เปิดวิทยุ" alone = resume this one
            p += " • สถานีล่าสุดที่ผู้ใช้ฟัง: ";
            p += String(li + 1).c_str();
            p += " ";
            p += last.name;
            p += " — ถ้าผู้ใช้สั่งเปิดวิทยุโดยไม่บอกสถานี ให้เรียก play_radio โดยไม่ใส่ station";
        }
        char playing[64] = "";
        if (gRadio.active() && !gRadio.isFile()) gRadio.info(playing, sizeof(playing), nullptr, 0);
        if (playing[0]) {  // after a wake-up the user usually wants something else - not the radio again
            p += " • ตอนนี้วิทยุกำลังเล่น: ";
            p += playing;
            p += " (เรียก play_radio เฉพาะเมื่อประโยคล่าสุดขอเปิดหรือเปลี่ยนสถานี อย่าพูดถึงวิทยุถ้าไม่ได้ถาม)";
        }
        p += "\n";
    }
    if (gMusic.count()) {  // T122: the MP3 player - play_radio with music
        char song[64] = "";
        if (gRadio.active() && gRadio.isFile()) gRadio.info(song, sizeof(song), nullptr, 0);
        p += "เพลงใน SD card: ";
        p += String((unsigned)gMusic.count()).c_str();
        p += " เพลง — ผู้ใช้ขอฟังเพลง (ไม่ใช่วิทยุ) ให้เรียก play_radio ใส่ music = คำในชื่อเพลง/ศิลปิน, \"next\" = เปลี่ยนเพลง/เพลงถัดไป, \"prev\" = เพลงก่อนหน้า, "
             "\"random\" = สุ่ม, \"*\" = เปิดเพลง (ระบบบอกชื่อเพลงที่เล่นจริงในผลลัพธ์ ให้พูดตามนั้น)";
        if (song[0]) {
            p += " • ตอนนี้กำลังเล่นเพลง: ";
            p += song;
            p += " (เรียกเฉพาะเมื่อผู้ใช้ขอเปลี่ยนเพลง)";
        }
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
    // "use when relevant": a remembered plan was raised on almost every "hello" (a trip the user never
    // planned, saved from radio audio - T054)
    p += "\n## สิ่งสำคัญที่คุณจำได้เกี่ยวกับผู้ใช้ (ใช้เมื่อเกี่ยวกับเรื่องที่ผู้ใช้พูด อย่ายกขึ้นมาเองตอนทักทาย)\n";
    p += core.empty() ? PsString("(ยังไม่มี ค่อยๆ ทำความรู้จักและบันทึกไว้)\n") : core;

    PsString recent;
    _episodes.buildRecentContext(recent, MEMORY_RECENT_TURNS, MEMORY_RECENT_PROMPT_BYTES, AGENT_USER_LABEL,
                                 AGENT_SELF_LABEL);
    if (!recent.empty()) {
        // the assistant's own lines are in here too: one greeting that raised an old topic used to be
        // fed back into the next session, which raised it again (a loop - T054)
        p += "\n## บทสนทนาล่าสุดก่อนเซสชันนี้ (บริบทเท่านั้น ไม่ต้องทวน อย่ายกเรื่องเก่าขึ้นมาเองตอนทักทาย และยึดโทนเสียง/คำลงท้ายตามโปรไฟล์ที่ล็อกไว้ปัจจุบันเท่านั้น)\n";
        PsString cleanRecent;  // T109: lines logged before the filter could still hold "set_expression(...)"
        transcript::stripToolCallText(recent.c_str(), cleanRecent);
        p += cleanRecent;
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
    if (_setupMode == SetupMode::SearchOnly) {  // diagnostic: is Search refused even with nothing else?
        _searchInSetup = false;                 // the answer is logged, not fed to search::Gate
        setup["tools"].to<JsonArray>().add<JsonObject>()["googleSearch"].to<JsonObject>();
        Serial.printf("[Agent] Setup: %s, SEARCH ONLY (diagnostic)\n", liveModelId(model));
        return;
    }
    const persona::VoiceInfo* vi = persona::findVoice(_cfg.voice);
    const char* voiceName = vi ? vi->name : (_cfg.voice[0] ? _cfg.voice : "Aoede");
    gen["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = voiceName;
    if (model == (uint8_t)LiveModel::Live38Thinking) {
        gen["thinkingConfig"]["thinkingLevel"] = thinkingLevelId(_cfg.thinking);
    }

    PsString prompt;
    buildSystemPrompt(prompt);
    setup["systemInstruction"]["parts"][0]["text"] = prompt.c_str();

    _searchInSetup = searchAvailable();
    _tools->setSearchOffered(_searchInSetup);
    if (_setupMode == SetupMode::OneTool) {  // diagnostic (T108): the full prompt but only `calculate`
        JsonObject f = setup["tools"].to<JsonArray>().add<JsonObject>()["functionDeclarations"].to<JsonArray>().add<JsonObject>();
        f["name"] = "calculate";
        f["description"] = "Evaluate an arithmetic expression exactly. Always use it for any math.";
        const ToolBehavior b = behaviorFor(model);
        if (b != ToolBehavior::Unspecified) f["behavior"] = b == ToolBehavior::NonBlocking ? "NON_BLOCKING" : "BLOCKING";
        f["parameters"]["type"] = "OBJECT";
        f["parameters"]["properties"]["expression"]["type"] = "STRING";
        f["parameters"]["required"].to<JsonArray>().add("expression");
        Serial.println("[Agent] Setup: ONE TOOL (calculate) - diagnostic");
    } else {
        _tools->addDeclarations(setup["tools"].to<JsonArray>(), behaviorFor(model), _searchInSetup);
    }

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
                  _resumeHandle.length() ? "resumed" : "new", voiceName, (unsigned)prompt.size(),
                  _searchInSetup ? "" : ", no Google Search", model != _cfg.model ? " (FALLBACK)" : "");
}

void AgentController::onSetupComplete() {
    if (_setupMode == SetupMode::SearchOnly) {
        Serial.printf("[Diag] SEARCH ONLY setup accepted by %s: Search itself is allowed - the full setup is what "
                      "fails (try: ask <question that needs the web>)\n", liveModelId(_activeModel));
    }
    _failedResumes = 0;
    _freshPending = false;  // the new session is up: its own resumption handles count from here
    _sessionGen++;  // web-tool answers from an older session are dropped
    _webPending = 0;
    _toolBatch.clear();
    _textTurn.clear();
    _nudgeAt = 0;  // a nudge for the old session's question
    _liveSel.reportSuccess(_activeModel);
    _fatalError = false;
    if (_searchInSetup) {
        if (_search.onReadyWithSearch()) saveSearchState(0, 0);  // Search works again: the doubling starts over
    } else {
        uint32_t epoch = 0, pause = 0;
        uint8_t strikes = 0;
        if (_search.onReadyWithoutSearch(monoSec(), clockEpoch(), epoch, strikes, pause)) {
            saveSearchState(epoch, strikes);
            Serial.printf("[Search] quota with Google Search, fine without it -> Search off for %lu h (%u in a row%s)\n",
                          (unsigned long)(pause / 3600), (unsigned)strikes, epoch ? ", kept across reboots" : "");
            _face->setHint("Google Search ใช้ไม่ได้ (โควตา) ปิดการค้นหาชั่วคราว", 3000);
        }
    }
    if (_activeModel != _cfg.model) {
        char buf[96];
        snprintf(buf, sizeof(buf), "ใช้ %s แทนชั่วคราว", liveModelLabel(_activeModel));
        _face->setHint(buf, 3000);
    }
    if (_wakeBuffering && _wakeHeard) {  // a silent tap wake: the mic loop lets go of it
        beginUtterance(millis(), 0.0f, false);  // fresh stats (they showed the previous sentence's)
        sendPreroll();
    } else if (_wokeByWord) {
        // T124: "Jarvis" ... silence: the user waits for an answer. The word itself is not sent (heard as Spanish), and
        // without a reply the radio rule closed the session 2-3 s after it was ready - say "ว่าไงคะ", then listen.
        // Anchor the locked voice & gender on Turn 1 so the session's first audio turn never drifts in pitch/gender.
        const persona::VoiceInfo* vi = persona::findVoice(_cfg.voice);
        PsString greet = "(ระบบ: ผู้ใช้เพิ่งเรียกชื่อคุณด้วยคำปลุก ขานรับสั้นๆ 1-4 คำอย่างเป็นกันเองด้วยเนื้อเสียง ";
        greet += vi ? vi->name : _cfg.voice;
        if (vi) {
            greet += " โทน";
            greet += vi->styleTh;
        }
        greet += " ";
        greet += persona::politeParticle((persona::Gender)_cfg.gender);
        greet += " ห้ามดัดเสียง แล้วรอฟังคำสั่ง ห้ามเรียก tool)";
        if (_gemini.sendClientText(greet.c_str(), true)) {
            Serial.println("[Chat] woken by the wake word, nothing said yet - greeting");
            _awaitingReply = true;
            _lastUserSpeech = millis();
            noteActivity();
        }
    }
    _wokeByWord = false;
}

void AgentController::onDisconnected(bool wasReady) {
    _replyAudio = false;  // a reply cut off by the drop: the next one discards stale mic audio again
    flushUserTurn();
    flushAgentTurn(!wasReady);
    _awaitingReply = false;
    _awaitToolAnswer = false;  // the new session never saw that result
    _reasoningAt = 0;
    _nudgeAt = 0;
    _vadOpen = false;
    _prerollDraining = false;  // the queue was dropped with the link
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
    if (_setupMode == SetupMode::SearchOnly && !_gemini.isReady()) {  // diagnostic answer, then back to normal
        Serial.printf("[Diag] SEARCH ONLY setup refused (%d %s) -> the key/project cannot use Search with %s\n", code,
                      fallback::failureName(f), liveModelId(_activeModel));
        _setupMode = SetupMode::Full;
        _resumeHandle = "";
        _reconnectPending = true;
        return;
    }
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
    if (f == fallback::Failure::SessionExpired) {  // stale / still-held resume handle: fresh session, same model
        Serial.println("[Agent] Resume handle expired or still held by the dropped link -> fresh session");
        _resumeHandle = "";
        _failedResumes = 0;
        _reconnectPending = true;
        return;
    }
    const uint32_t sec = monoSec();
    // Grounding with Google Search has its own quota, and when it is used up the server closes the
    // whole session with the same "exceeded your current quota" text (verified 2026-10-14: every
    // live model failed with search, all worked without). But a session left open by a reboot
    // without a close handshake gives the same text (T035), so Search is only SUSPECT here: retry
    // the same model without it - if that session gets ready, it was Search (onSetupComplete); if it
    // fails with quota too, it was not (below). A REST check cannot tell: free-tier REST models have
    // no Search grounding at all (always 429), the Live models do - T045, T048.
    if (f == fallback::Failure::Quota && _searchInSetup) {
        if (_search.onLiveQuota(sec)) {
            Serial.printf("[Fallback] quota error with Google Search -> retrying %s without search to see which it is\n",
                          liveModelId(_activeModel));
        }
        _resumeHandle = "";
        _reconnectPending = true;
        return;
    }
    if (f == fallback::Failure::Quota && _search.onQuotaWithoutSearch(sec)) {
        Serial.println("[Search] quota without Google Search as well: Search was not the cause (a session left "
                       "open by a reboot, or the model quota) - it stays on");
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
    return _cfg.googleSearch && _setupMode == SetupMode::Full && _search.available(monoSec(), clockEpoch());
}

void AgentController::saveSearchState(uint32_t epoch, uint8_t strikes) {
    Preferences rt;
    if (!rt.begin("rt", false)) return;
    if (epoch) rt.putUInt("srchoff", epoch);
    else if (rt.isKey("srchoff")) rt.remove("srchoff");
    if (strikes) rt.putUChar("srchn", strikes);
    else if (rt.isKey("srchn")) rt.remove("srchn");
    rt.end();
}

void AgentController::handleSearchGate() {
    const uint32_t epoch = clockEpoch();
    if (_search.savedExpired(epoch)) {  // the pause saved before a reboot is over
        _search.clearSaved();
        saveSearchState(0, _search.strikes());
        Serial.println("[Search] Google Search pause is over - offered again");
    }
    uint32_t persist = 0;
    if (_search.needsSave(monoSec(), epoch, persist)) saveSearchState(persist, _search.strikes());  // confirmed before SNTP
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
    if (!_replyAudio) {  // first audio of this reply: free the uplink for it
        cancelNudge();
        _replyAudio = true;
        _gemini.discardQueuedMic();
        _gemini.takeTurnReadMs();  // per-reply network figure starts now
        _replyT0 = millis();
        _replyBytes0 = _gemini.audioBytesReceived();  // already counts this first message
    }
    _replyLastMs = millis();
    _awaitingReply = false;
    _ringing = false;
    _lastAgentAudio = millis();
    markAnswered();
    noteActivity();
    _audio->writeVoice((const uint8_t*)pcm, samples * 2);
}

void AgentController::onInputTranscript(const char* text) {
    if (!_agentTurn.empty()) flushAgentTurn(false);
    if (_userTurn.empty()) _userTurnTs = nowEpoch();
    cancelNudge();  // the user said something new: that gets its own answer
    _userTurn += text;
    _lastUserSpeech = millis();
    _awaitingReply = true;
    _heardSinceSend = true;
    noteActivity();
    _face->pushTranscript('u', text);
}

void AgentController::onOutputTranscript(const char* raw) {
    if (_dropAudio) return;
    PsString clean;  // 3.1 Flash Live leaks "functionreponsetext..." after parallel tool calls
    transcript::stripToolScaffolding(raw, clean);
    PsString spoken;  // T109: "set_expression(emotion=neutral)" said out loud - not shown / logged
    transcript::stripToolCallText(clean.c_str(), spoken);
    clean = spoken;
    if (clean.empty()) return;
    const char* text = clean.c_str();
    cancelNudge();
    if (!_userTurn.empty()) flushUserTurn();
    if (_agentTurn.empty()) _agentTurnTs = nowEpoch();
    _agentTurn += text;
    autoExpression();
    _awaitingReply = false;
    markAnswered();
    _face->pushTranscript('a', text);
}

// T140: the eyes follow what is said - the new words plus a little before them (a word split across chunks)
void AgentController::autoExpression() {
    if (_cueFrom > _agentTurn.size()) _cueFrom = 0;  // a new turn
    const size_t from = _cueFrom > 36 ? _cueFrom - 36 : 0;
    Emotion e;
    if (emotioncue::find(_agentTurn.c_str() + from, e)) _face->setExpression(e, AGENT_EXPRESSION_HOLD_MS);
    _cueFrom = _agentTurn.size();
}

// T111: the model went on by itself (speech, a tool call) or the user spoke again - the pending nudge is not needed
void AgentController::cancelNudge() {
    if (!_nudgeAt) return;
    _nudgeAt = 0;
    _nudgeText.clear();
    Serial.println("[Chat] the turn went on - no nudge needed");
}

void AgentController::onInterrupted() {
    _audio->clearPlayback();
    flushAgentTurn(true);
    _dropAudio = false;
}

void AgentController::onGenerationComplete() { _audio->flushPlayback(); }

void AgentController::onTurnComplete() {
    // T108: Extended Thinking sends turnComplete with interactionStatus IN_PROGRESS while it keeps reasoning in the
    // background - more speech / tool calls follow. Not an end: no nudge (a text turn would interrupt it), and the
    // session stays awake for the rest (bounded in checkNoReply).
    const bool reasoning = _gemini.interactionInProgress();
    Serial.printf("[Chat] turn complete (tokens/min %lu)%s\n", (unsigned long)tokensPerMinute(),
                  reasoning ? " - still reasoning" : "");
    _reasoningAt = reasoning ? millis() : 0;
    // T107: gemini-3.8-live sometimes ends the turn without a word right after a web tool result that took a few
    // seconds (the same result from the cache a minute later was answered). Ask once for the answer.
    // T110: an empty turn that started another lookup (the model chaining tools: gold price -> Thai gold news) is not
    // a missing answer - nudging it made the model fetch the same quote again and again (69k tokens / minute)
    // T111: none of them is sent at once - see kNudgeDelayMs
    bool nudged = false;
    if (!reasoning && _awaitToolAnswer && _webPending == 0 && !_replyAudio && _agentTurn.empty() && _gemini.isReady()) {
        _awaitToolAnswer = false;
        nudged = true;
        _nudgeWhy = "empty turn after a tool result - asking for the answer";
        _nudgeText = "(ระบบ: ผลของเครื่องมือมาถึงแล้ว ตอบคำถามล่าสุดของผู้ใช้จากผลนั้นด้วยน้ำเสียงและคำลงท้ายตามโปรไฟล์ที่ล็อกไว้เลย)";
        _nudgeAt = millis();
    } else if (_replyAudio || !_agentTurn.empty()) {
        // T110: a "let me check" that began before the result went out is not the answer to it
        if (!_replyAudio || (int32_t)(_replyT0 - _toolAnswerAt) >= 0) _awaitToolAnswer = false;
    } else if (!reasoning && _webPending == 0 && (_userTurn.size() >= 6 || !_textTurn.empty()) && _gemini.isReady() &&
               !gRadio.active() && !_sleepPending && !_mutePending) {
        // T109: the user was heard (transcript) but the turn ended empty (~12 reply tokens, no audio): a user had to
        // ask twice ("ราคาทองคำปัจจุบันเท่าไหร่" - the repeat was answered). Proactive audio judging it not meant
        // for the model, or a glitch. Hand it over once more as text. (Not with the radio on: that is not for it.
        // T110: not with a lookup still running in the background either - the answer is on its way. A typed message
        // or reminder can end the same way: "ระดับน้ำที่อุบลฯ" got only a set_expression.)
        nudged = true;
        if (_userTurn.size() >= 6) {
            _nudgeWhy = "heard the user but the turn ended without an answer - asking once";
            _nudgeText = "(ระบบ: ผู้ใช้เพิ่งพูดกับคุณว่า \"";
            _nudgeText += _userTurn;
            _nudgeText += "\" แต่คุณยังไม่ได้ตอบ ตอบด้วยน้ำเสียงและคำลงท้ายตามโปรไฟล์ที่ล็อกไว้เลย)";
        } else {
            _nudgeWhy = "text turn ended without an answer - asking once";
            _nudgeText = "(ระบบ: ข้อความนี้ยังไม่ได้รับคำตอบ ตอบด้วยน้ำเสียงและคำลงท้ายตามโปรไฟล์ที่ล็อกไว้เลย) ";
            _nudgeText += _textTurn;
        }
        _nudgeAt = millis();
    }
    _textTurn.clear();
    flushUserTurn();
    flushAgentTurn(_dropAudio);
    _audio->flushPlayback();
    _dropAudio = false;
    _awaitingReply = nudged || reasoning;  // the nudge is a question too / the reasoning model owes the rest
    if (_replyAudio) {
        // arrival rate: audio after the first message vs the time it took to come in (1.0 = real time)
        const uint32_t wall = _replyLastMs - _replyT0;
        const uint32_t audioMs = (_gemini.audioBytesReceived() - _replyBytes0) / (AUDIO_I2S_RATE * 2 / 1000);
        _replyRate = wall >= 1500 ? (float)audioMs / (float)wall : 0.0f;  // short replies say little
        _replyNeedMs = _replyRate > 0.0f && _replyRate < 1.0f ? (uint32_t)((float)audioMs * (1.0f - _replyRate)) : 0;
        _replyReportPending = true;  // one line once the reply has finished playing
    }
    _replyAudio = false;
}

// After each spoken reply: how smooth it was, so a user's log shows why it stuttered
void AgentController::reportReply() {
    if (!_replyReportPending || _audio->isPlaying()) return;
    _replyReportPending = false;
    const AudioManager::ReplyStats s = _audio->takeReplyStats();
    const uint32_t netMs = _gemini.takeTurnReadMs();
    const uint32_t stale = _gemini.takeMicDiscarded();
    const size_t dropped = _audio->takePlaybackDropped();
    // +20 % headroom: the next reply may be longer or the link a little slower
    if (_replyRate > 0.0f) _audio->tunePrebuffer(_replyNeedMs * 6 / 5, s.gaps > 0);
    char rate[48] = "-";
    if (_replyRate > 0.0f) snprintf(rate, sizeof(rate), "%d%%", (int)(_replyRate * 100.0f + 0.5f));
    Serial.printf("[Audio] จบคำตอบ: กระตุก %lu ครั้ง (นานสุด %lu ms) | บัฟเฟอร์ %lu -> %lu ms"
                  " | เน็ตส่งเสียงทัน %s ของเวลาจริง (ต่ำกว่า 100%% = เน็ตช้ากว่าเสียง)"
                  " | ก้อนหนึ่งมาช้าสุด %lu ms | ลำโพงรอนานสุด %lu ms | ทิ้งเสียงไมค์ค้างส่ง %lu ก้อน"
                  " | เสียงล้นบัฟเฟอร์ %u KB\n",
                  (unsigned long)s.gaps, (unsigned long)s.longestGapMs, (unsigned long)s.prebufferMs,
                  (unsigned long)_audio->prebufferMs(), rate, (unsigned long)netMs, (unsigned long)s.outGapMaxMs,
                  (unsigned long)stale, (unsigned)(dropped / 1024));
}

void AgentController::onToolCall(JsonArrayConst calls) {
    noteActivity();
    markAnswered();
    for (JsonObjectConst call : calls) {  // a set_expression alone is what the empty turn already had (T111)
        if (strcmp(call["name"] | "", "set_expression") != 0) {
            cancelNudge();
            break;
        }
    }
    JsonDocument resp(JsonPsramAllocator::instance());
    JsonArray responses = resp["toolResponse"]["functionResponses"].to<JsonArray>();
    for (JsonObjectConst call : calls) {
        const char* name = call["name"] | "";
        if (!strcmp(name, "vehicle") && deferVehicleCall(call)) continue;  // reading the car's codes: answered later
        if (webtools::isWebTool(name)) {  // HTTPS in the webTools task; answered later with the same id
            String args;
            serializeJson(call["args"], args);
            if (_web.submit(call["id"] | "", name, args.c_str(), _sessionGen)) {
                if (_webPending < 255) _webPending++;
                Serial.printf("[Tool] %s %s -> fetching...\n", name, args.c_str());
                _face->setHint("กำลังดึงข้อมูล...", 2500);
                _toolBusy.arm(millis(), 15000);
                continue;
            }
        }
        JsonObject fr = responses.add<JsonObject>();
        if (!call["id"].isNull()) fr["id"] = call["id"];
        fr["name"] = name;
        JsonObject response = fr["response"].to<JsonObject>();
        // gemini-3.8-live: set_expression is NON_BLOCKING - answered SILENT, or the model is prompted
        // again once its reply is done and says it a second time (T071); background lookups WHEN_IDLE (T110)
        if (const char* s = AgentTools::responseScheduling(name, behaviorFor(_activeModel))) fr["scheduling"] = s;
        if (webtools::isWebTool(name)) {
            // its queue is full: AgentTools does not know web tools and answered "unknown function", after
            // which the model stopped using the tool for the whole session (T069)
            // T105: the model said "done, see the chart" and quoted older numbers after this - spell it out
            response["error"] =
                "busy: an earlier lookup is still running, so you got NO data now. Tell the user it is not ready / "
                "failed for now; never make up results, never reuse numbers from earlier, nothing new is on screen.";
            Serial.printf("[Tool] %s -> busy (web tool queue full)\n", name);
            continue;
        }
        const bool ok = _tools->execute(name, call["args"].as<JsonObjectConst>(), response);
        if (strcmp(name, "set_expression") != 0) {
            String args;  // what the model asked for (e.g. which radio station) - memory tools stay private
            const bool privateArgs = !strncmp(name, "save_", 5) || !strcmp(name, "notes");
            if (!privateArgs && call["args"].is<JsonObjectConst>() && call["args"].size()) serializeJson(call["args"], args);
            Serial.printf("[Tool] %s %s-> %s\n", name, args.length() ? (args + " ").c_str() : "",
                          ok ? "ok" : (response["error"] | "error"));
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
    } else if (_toolBusy.remaining(millis()) < 600) {
        _toolBusy.arm(millis(), 600);
    }
    if (responses.size()) {
        if (!_gemini.sendJson(resp)) Serial.println("[Tool] ERROR: failed to send toolResponse");
        // the model answers after the toolResponse: a pending sleep (go_to_sleep, play_radio) must wait
        // for that reply instead of closing the session in the gap before its first audio
        else if (_sleepPending) _awaitingReply = true;
    }
}

// Finished web-tool calls: answer the model (same session only) or print for the console / PC app
void AgentController::handleWebResults() {
    // T105: a web job that stops moving (one froze the console - its task starves loopTask) - say where, from here
    static uint32_t lastStuckReport = 0;
    const uint32_t stuckFor = millis() - webtools::gStageAt;
    // ("idle" = the job was queued a moment ago and has not started: not stuck - a false report in a user's log)
    if (_web.busy() && strcmp(webtools::gStage, "idle") != 0 && stuckFor > 45000 && millis() - lastStuckReport > 15000) {
        lastStuckReport = millis();
        Serial.printf("[Web] job stuck? stage \"%s\" for %lu s\n", webtools::gStage, (unsigned long)(stuckFor / 1000));
    }
    webtools::Result r;
    while (_web.poll(r)) {
        JsonDocument result(JsonPsramAllocator::instance());
        if (deserializeJson(result, r.json->c_str()) || !result.is<JsonObject>()) {
            // out of memory / cut short: answer with an error rather than an empty response
            result.clear();
            result["error"] = "tool result could not be read";
        }
        if (!strcmp(r.name, "price_alert") || r.tag == webtools::kAlertTag) _priceAlertsDirty = true;  // T141
        if (r.tag == webtools::kAlertTag) {  // a price alert fired in the background
            const char* m = result["message"] | "";
            if (!*m) {
                delete r.json;
                continue;
            }
            Serial.printf("[Alert] %s\n", m);
            PsString msg = "[แจ้งเตือนราคาจากระบบ] ";
            msg += m;
            msg += " — บอกผู้ใช้สั้นๆ";
            announce(msg);
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
            if (_webPending) _webPending--;
            if (!_toolBatch.size()) _toolBatchAt = millis();
            JsonObject fr = _toolBatch["toolResponse"]["functionResponses"].add<JsonObject>();
            if (r.id[0]) fr["id"] = r.id;
            fr["name"] = r.name;
            fr["response"] = result.as<JsonObjectConst>();
            // T110: a background lookup on gemini-3.8-live - told once the "let me check" has finished
            if (const char* s = AgentTools::responseScheduling(r.name, behaviorFor(_activeModel))) fr["scheduling"] = s;
        } else {
            Serial.printf("[Tool] %s result dropped (session changed)\n", r.name);
        }
        if (!ok) _face->setHint("ดึงข้อมูลไม่สำเร็จ", 2000);
        _toolBusy.arm(millis(), 600);
        delete r.json;
    }
    // the last running lookup is in (or one never came back: a result the web task could not queue)
    if (_toolBatch.size() && (_webPending == 0 || millis() - _toolBatchAt > 30000)) {
        _webPending = 0;
        if (!_gemini.isReady()) {
            Serial.println("[Tool] results dropped (session closed)");
        } else if (!_gemini.sendJson(_toolBatch)) {
            Serial.println("[Tool] ERROR: failed to send toolResponse");
        } else {
            _awaitToolAnswer = true;  // T107: an empty turn after this gets one nudge
            _toolAnswerAt = millis();
        }
        _toolBatch.clear();
    }
}

void AgentController::onToolCallCancellation(JsonArrayConst ids) {
    Serial.printf("[Tool] %u call(s) cancelled by the server\n", (unsigned)ids.size());
}

void AgentController::onSessionHandle(const char* handle, bool resumable) {
    // a "new session" was asked for: updates still queued from the old one (read before its close)
    // used to restore the handle, and the next setup resumed the old session after all
    if (_freshPending) return;
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
