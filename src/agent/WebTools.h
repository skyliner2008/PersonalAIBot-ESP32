/**
 * @file WebTools.h
 * @brief Live-data tools (weather, Thai fuel prices, TradingView quotes, FX, news) with on-screen cards
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * HTTPS calls take 1-3 s, so they run in their own task ("webTools", core 0): the agent submits a
 * function call, keeps streaming audio, and later sends the toolResponse with the same id.
 * Every tool also fills a CardData and hands it to gCard (shown over the face while the AI speaks).
 *
 * Sources (no API key needed, TLS verified against WEB_ROOT_CA_PEM):
 *   weather  Open-Meteo forecast + air-quality + geocoding (Thai names, English fallback)
 *   fuel     oil-price.bangchak.co.th/ApiOilPrice2/th (today / yesterday / tomorrow)
 *   markets  scanner.tradingview.com/symbol (same endpoint the PersonalAIBot app uses)
 *   news     news.google.com RSS, Thai edition (streamed: only the first headlines are read)
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "../util/PsramAlloc.h"

namespace webtools {

/** True for the tools this module runs (declared in AgentTools::addDeclarations). */
bool isWebTool(const char* name);

/** Result.tag of a fired price alert (name "_price_alert", json = {"message": "..."}). */
constexpr uint32_t kAlertTag = 0xFFFFFFFFu;

struct Result {
    char id[64];
    char name[32];
    uint32_t tag;         // caller-defined (session generation / console)
    uint32_t elapsedMs;
    PsString* json;       // {"result":{...}} or {"error":"..."}; owned by the receiver
};

class WebTools {
public:
    /** `storageDir`: agent data dir for notes / price alerts ("" = none). */
    bool begin(const char* storageDir);

    /** Queue a call; `argsJson` is copied. False if the queue is full. */
    bool submit(const char* id, const char* name, const char* argsJson, uint32_t tag);
    /** Non-blocking: a finished call (caller deletes result.json). */
    bool poll(Result& out);
    bool busy() const { return _busy; }

private:
    struct Job {
        char id[64];
        char name[32];
        uint32_t tag;
        char* args;  // PSRAM
    };
    QueueHandle_t _jobs = nullptr;
    QueueHandle_t _results = nullptr;
    volatile bool _busy = false;
    char _dir[64] = "";

    static void taskEntry(void* self);
    void loop();
};

}  // namespace webtools
