/**
 * @file WebToolsCommon.h
 * @brief Helpers shared by the web tools (HTTP(S), URL encoding, TradingView batch scan, ...)
 *
 * Only used inside the webTools task (WebTools.cpp / WebToolsExtra.cpp).
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <vector>

#include "../ai/GoogleRootCA.h"
#include "../ui/CardScreen.h"
#include "../util/JsonAlloc.h"
#include "../util/PsramAlloc.h"
#include "../util/Utf8.h"

namespace webtools {

constexpr uint32_t kHttpTimeoutMs = 12000;

inline String urlEncode(const char* s) {
    static const char kHex[] = "0123456789ABCDEF";
    String out;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            out += (char)*p;
        } else {
            out += '%';
            out += kHex[*p >> 4];
            out += kHex[*p & 15];
        }
    }
    return out;
}

/**
 * GET (body == nullptr) or POST JSON. https:// is verified against WEB_ROOT_CA_PEM; http:// is
 * allowed for LAN services (the MT5 bridge). `bearer` adds "Authorization: Bearer ...".
 */
inline int httpRequest(const String& url, String& response, const char* body = nullptr, const char* bearer = nullptr) {
    if (WiFi.status() != WL_CONNECTED) return -10;
    const bool tlsOn = url.startsWith("https://");
    WiFiClientSecure tls;
    WiFiClient plain;
    if (tlsOn) {
        tls.setCACert(WEB_ROOT_CA_PEM);
        tls.setTimeout(kHttpTimeoutMs / 1000);
    }
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    http.setUserAgent("Mozilla/5.0 (ESP32-AI personal assistant)");
    const bool ok = tlsOn ? http.begin(tls, url) : http.begin(plain, url);
    if (!ok) return -2;
    if (bearer && *bearer) http.addHeader("Authorization", String("Bearer ") + bearer);
    int code;
    if (body) {
        http.addHeader("Content-Type", "application/json");
        code = http.POST((uint8_t*)body, strlen(body));
    } else {
        code = http.GET();
    }
    if (code > 0) response = http.getString();  // > 4 KB goes to PSRAM
    http.end();
    return code;
}

inline int httpGet(const String& url, String& body) { return httpRequest(url, body); }

inline void copyStr(char* dst, size_t n, const char* src) {  // never splits a UTF-8 character
    if (!src) src = "";
    const size_t len = utf8::clampToBoundary(src, strlen(src), n - 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

// 0 = Sunday (Sakamoto)
inline int weekday(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

inline const char* thaiWeekdayShort(int wd) {
    static const char* const kDay[] = {"อา.", "จ.", "อ.", "พ.", "พฤ.", "ศ.", "ส."};
    return kDay[(wd % 7 + 7) % 7];
}

inline void setError(JsonDocument& out, const char* msg) {
    out.clear();
    out["error"] = msg;
}

/** "1,234.56" / "12.3K" style number for cards */
inline void fmtNum(char* out, size_t n, double v, int decimals = 2) {
    const double a = fabs(v);
    if (a >= 1e12) snprintf(out, n, "%.2fT", v / 1e12);
    else if (a >= 1e9) snprintf(out, n, "%.2fB", v / 1e9);
    else if (a >= 1e6) snprintf(out, n, "%.2fM", v / 1e6);
    else snprintf(out, n, "%.*f", decimals, v);
}

/**
 * TradingView batch quote: POST scanner.tradingview.com/global/scan {"symbols":{"tickers":[...]}}
 * One request for any mix of markets (~1 s). `doc` receives the raw response.
 */
inline bool scanTickers(const std::vector<String>& tickers, const char* columnsJson, JsonDocument& doc) {
    String body = "{\"symbols\":{\"tickers\":[";
    for (size_t i = 0; i < tickers.size(); i++) {
        if (i) body += ',';
        body += '"';
        body += tickers[i];
        body += '"';
    }
    body += "]},\"columns\":";
    body += columnsJson;
    body += '}';
    String resp;
    if (httpRequest("https://scanner.tradingview.com/global/scan", resp, body.c_str()) != 200) return false;
    doc.clear();
    return !deserializeJson(doc, resp);
}

/** TradingView symbols to try for a user token (alias / EXCHANGE:SYM / bare ticker -> SET, NASDAQ, NYSE). */
int tickerCandidates(String token, String out[3], const char** aliasName);

// ---- tools implemented in WebToolsExtra.cpp ----
struct ToolEnv {
    const char* storageDir;  // agent data dir on SD / LittleFS ("" if none)
};
void runCalculate(JsonObjectConst a, JsonDocument& out, CardData& card);
void runConvertUnits(JsonObjectConst a, JsonDocument& out, CardData& card);
void runWorldTime(JsonObjectConst a, JsonDocument& out, CardData& card);
void runLocation(JsonDocument& out, CardData& card);
bool ipLocation(float& lat, float& lon, String& label);  // cached (also used by get_weather)
void runKnowledge(JsonObjectConst a, JsonDocument& out, CardData& card);
void runMarketOverview(JsonObjectConst a, JsonDocument& out, CardData& card);
void runMarketAnalysis(JsonObjectConst a, JsonDocument& out, CardData& card);
void runMarketScan(JsonObjectConst a, JsonDocument& out, CardData& card);
void runEconomicCalendar(JsonObjectConst a, JsonDocument& out, CardData& card);
void runPositionSize(JsonObjectConst a, JsonDocument& out, CardData& card);
void runNotes(const ToolEnv& env, JsonObjectConst a, JsonDocument& out, CardData& card);
void runPriceAlert(const ToolEnv& env, JsonObjectConst a, JsonDocument& out, CardData& card);
/** Check stored alerts; fills `message` (Thai) and returns true when one fired (it is removed). */
bool checkPriceAlerts(const ToolEnv& env, String& message);
bool hasPriceAlerts(const ToolEnv& env);
void runMt5Account(JsonObjectConst a, JsonDocument& out, CardData& card);
void runMt5Trade(JsonObjectConst a, JsonDocument& out, CardData& card);
/** Find an Internet radio station (Radio Browser) - the agent starts playback from the result. */
void runRadio(JsonObjectConst a, JsonDocument& out, CardData& card);

}  // namespace webtools
