/**
 * @file WifiLink.h
 * @brief Wi-Fi state shared by the agent (it owns the connection) and the settings menu (T102)
 *
 * The menu shows which network is really joined (a remembered one when the configured one is not around, T099)
 * and can ask the agent to join the configured network right away instead of waiting for the next retry.
 */

#pragma once

#include <WiFi.h>

#include <atomic>

inline std::atomic<uint8_t> gWifiReason{0};         // last STA disconnect reason (Wi-Fi event task)
inline std::atomic<uint8_t> gWifiPrimaryReason{0};  // why the configured network last failed (0 = joined / not tried)
inline std::atomic<bool> gWifiJoinPrimaryNow{false};  // menu -> agent: join the configured network now

/** The router refused the password (not "not found" / "weak signal"): the menu asks for it again. */
inline bool wifiReasonIsAuth(uint8_t r) {
    return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

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
