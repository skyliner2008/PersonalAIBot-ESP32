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
#include "../app/AppConfig.h"
#include "../ui/CardScreen.h"
#include "../util/DataDates.h"
#include "../util/JsonAlloc.h"
#include "../util/PsramAlloc.h"
#include "../util/Utf8.h"

namespace webtools {

constexpr uint32_t kHttpTimeoutMs = 12000;
// A stalled TLS handshake (lost packets on a phone hotspot) otherwise hangs for the framework's 120 s
// while the model waits in silence; a normal one takes 1-3 s here (Open-Meteo is ~200 ms away)
constexpr unsigned long kTlsHandshakeSec = 10;

/** Unix time, 0 while the clock is not synced. */
inline uint32_t epochNow() {
    const time_t t = time(nullptr);
    return t >= (time_t)timefmt::kMinValidEpoch ? (uint32_t)t : 0;
}

/** "2026-10-15 21:40" Thai time ("" while the clock is not synced). */
inline String thaiDateTime(uint32_t epoch) {
    char buf[20];
    if (!timefmt::formatDateTime(epoch, TIME_UTC_OFFSET_SEC, buf, sizeof(buf))) return String();
    return String(buf);
}

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
 * GET (body == nullptr) or POST JSON over https://, verified against WEB_ROOT_CA_PEM.
 * Plain http:// is refused (-3): every data source used by the tools is HTTPS.
 */
inline int httpRequest(const String& url, String& response, const char* body = nullptr,
                       uint32_t timeoutMs = kHttpTimeoutMs) {
    if (WiFi.status() != WL_CONNECTED) return -10;
    if (!url.startsWith("https://")) return -3;
    const int slash = url.indexOf('/', 8);
    const String host = url.substring(8, slash < 0 ? url.length() : slash);
    const uint32_t t0 = millis();
    uint32_t tDns = 0, tTls = 0, tReply = 0;
    int code = -1;
    auto report = [&]() {  // one line per request: where the time of a slow lookup went
        const uint32_t t = millis();
        Serial.printf("[Web] %s -> %d in %lu ms (dns %lu, tls %lu, reply %lu, body %lu; %u B)\n", host.c_str(), code,
                      (unsigned long)(t - t0), (unsigned long)(tDns ? tDns - t0 : t - t0),
                      (unsigned long)(tTls ? tTls - tDns : 0), (unsigned long)(tReply ? tReply - tTls : 0),
                      (unsigned long)(tReply ? t - tReply : 0), (unsigned)response.length());
    };
    // resolved apart from the connect (lwIP keeps it for the connect) so the log tells DNS from TLS
    IPAddress ip;
    if (WiFi.hostByName(host.c_str(), ip) != 1) {
        code = -11;
        report();
        return code;
    }
    tDns = millis();
    WiFiClientSecure tls;
    tls.setCACert(WEB_ROOT_CA_PEM);
    tls.setHandshakeTimeout(kTlsHandshakeSec);  // the framework's default is 120 s; timeoutMs does not cover it
    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setConnectTimeout(timeoutMs);
    http.setUserAgent("Mozilla/5.0 (ESP32-AI personal assistant)");
    // begin() BEFORE connecting: on a client that is already connected it sees a "new host" and drops the
    // connection - a second handshake per request (T072). Connected here, GET()/POST() reuse it.
    if (!http.begin(tls, url)) return -2;
    if (!tls.connect(host.c_str(), 443, (int32_t)timeoutMs)) {
        code = -1;
        report();
        return code;
    }
    tTls = millis();
    if (body) {
        http.addHeader("Content-Type", "application/json");
        code = http.POST((uint8_t*)body, strlen(body));
    } else {
        code = http.GET();
    }
    tReply = millis();
    if (code > 0) response = http.getString();  // > 4 KB goes to PSRAM
    http.end();
    report();
    return code;
}

inline int httpGet(const String& url, String& body, uint32_t timeoutMs = kHttpTimeoutMs) {
    return httpRequest(url, body, nullptr, timeoutMs);
}

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
// water level / rain / storms from ThaiWater (WaterTools.cpp, T100)
void runWater(JsonObjectConst a, JsonDocument& out, CardData& card);
/** Check stored alerts; fills `message` (Thai) and returns true when one fired (it is removed). */
bool checkPriceAlerts(const ToolEnv& env, String& message);
bool hasPriceAlerts(const ToolEnv& env);

}  // namespace webtools
