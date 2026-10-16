/**
 * @file AgentInternal.h
 * @brief Helpers shared by the AgentController*.cpp files (implementation detail, not a public API)
 */

#pragma once

#include "AgentController.h"

#include <Preferences.h>
#include <WiFi.h>
#include <atomic>
#include <math.h>
#include <time.h>

#include "../app/AppConfig.h"
#include "../audio/RadioPlayer.h"
#include "../audio/RadioStations.h"
#include "../system/OtaUpdater.h"
#include "../ui/FaceScreen.h"
#include "../ui/UiManager.h"
#include "../util/Deadline.h"
#include "../util/JsonAlloc.h"
#include "../util/TimeFmt.h"
#include "../util/TranscriptClean.h"
#include "Personas.h"
#include "WebTools.h"

namespace agentdetail {

inline constexpr size_t kChunk = AUDIO_UPLOAD_RATE * AUDIO_MIC_CHUNK_MS / 1000;  // 640 samples
inline constexpr size_t kPrerollSamples = AUDIO_UPLOAD_RATE * AGENT_VAD_PREROLL_MS / 1000;
inline constexpr size_t kWakeBufferSamples = AUDIO_UPLOAD_RATE * 6;  // 6 s spoken while the session connects (PSRAM; connecting takes 2-4 s)
inline constexpr uint32_t kWifiRetryMinMs = 3000;   // back-off between Wi-Fi attempts: 3 s doubling to 60 s
inline constexpr uint32_t kWifiRetryMaxMs = 60000;   // (routers block clients that retry every second)
inline constexpr uint32_t kThinkingTimeoutMs = 12000;
inline constexpr uint32_t kLateReminderSec = 15 * 60;  // fired this late = missed while the board was off
inline constexpr uint32_t kSearchThinkingTimeoutMs = AGENT_NO_REPLY_SEARCH_MS;  // Google Search answers take longer

inline std::atomic<uint8_t> gWifiReason{0};  // last STA disconnect reason (Wi-Fi event task)

inline const char* wifiReasonTh(uint8_t r) {
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

inline ltm::MemoryStore::Options memoryOptions() {
    ltm::MemoryStore::Options o;
    o.maxRecords = MEMORY_MAX_RECORDS;
    return o;
}

inline ltm::EpisodicLog::Options episodeOptions() {
    ltm::EpisodicLog::Options o;
    o.utcOffsetSec = TIME_UTC_OFFSET_SEC;
    return o;
}

inline float rms(const int16_t* s, size_t n) {
    float acc = 0;
    for (size_t i = 0; i < n; i++) {
        const float v = s[i] / 32768.0f;
        acc += v * v;
    }
    return n ? sqrtf(acc / n) : 0.0f;
}

inline uint16_t eyeColor565(uint8_t idx) {
    const persona::EyeColor& c = persona::kEyeColors[idx < persona::kEyeColorCount ? idx : 0];
    return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

// Unix time, 0 until SNTP has set the clock (same rule as AgentController::nowEpoch, usable in const code)
inline uint32_t clockEpoch() {
    const time_t t = time(nullptr);
    return t >= (time_t)timefmt::kMinValidEpoch ? (uint32_t)t : 0;
}

inline ToolBehavior behaviorFor(uint8_t model) {
    switch ((LiveModel)model) {
        case LiveModel::Live38: return ToolBehavior::Blocking;
        case LiveModel::Live38Thinking: return ToolBehavior::NonBlocking;  // BLOCKING is a hard error
        default: return ToolBehavior::Unspecified;                         // 3.1: sequential calls only
    }
}

}  // namespace agentdetail

using namespace agentdetail;  // NOLINT: only included by the AgentController*.cpp files
