/**
 * @file WebTools.h
 * @brief Live-data tools (weather, Thai fuel prices, TradingView quotes, FX, news) with on-screen cards
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * HTTPS calls take 1-3 s, so they run in their own task ("webTools", core 1, priority 4 while a job
 * runs - see JobPriority): the agent submits a function call, keeps streaming audio, and later sends
 * the toolResponse with the same id.
 * Every tool also fills a CardData and hands it to gCard (shown over the face while the AI speaks).
 *
 * Sources (no API key needed, TLS verified against WEB_ROOT_CA_PEM):
 *   weather  Open-Meteo forecast + air-quality + geocoding (Thai names, English fallback)
 *   fuel     oil-price.bangchak.co.th/ApiOilPrice2/th (today / yesterday / tomorrow)
 *   markets  scanner.tradingview.com/symbol (same endpoint the PersonalAIBot app uses)
 *   news     news.google.com RSS, Thai edition (streamed: only the first headlines are read)
 *   water    api-v3.thaiwater.net (the public API of thaiwater.net): water level, rain, storms (WaterTools.cpp)
 */

#pragma once

#include <vector>

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "../util/PsramAlloc.h"

namespace webtools {

/** True for the tools this module runs (declared in AgentTools::addDeclarations). */
bool isWebTool(const char* name);

/**
 * What the webTools task is doing right now and since when (T105): a job that stops moving starves the console
 * task, so the agent (higher priority) reports the stage instead.
 */
inline const char* volatile gStage = "idle";
inline volatile uint32_t gStageAt = 0;
inline void setStage(const char* s) {
    gStage = s;
    gStageAt = millis();
}

/** Result.tag of a fired price alert (name "_price_alert", json = {"message": "..."}). */
constexpr uint32_t kAlertTag = 0xFFFFFFFFu;

/** Compact weather snapshot for power-save clock / screensaver themes. */
struct ClockWeather {
    bool valid = false;
    float temp = 0.0f;
    int16_t humidity = 0;
    int16_t pm25 = -1;
    int8_t code = 0;
    bool isDay = true;
    char city[48] = "";
};

/** Thread-safe read of the latest weather snapshot for the screensaver clock. */
ClockWeather getClockWeather();

/** Trigger an early background refresh (e.g. when the user changes weatherCity in Settings). */
void requestClockWeatherRefresh();

/** T141: one price alert for the alerts screen (alerts.json in the agent dir). */
struct PriceAlertInfo {
    int id = 0;
    char symbol[32] = "";
    char name[48] = "";
    double target = 0;  // the price it waits for
    bool above = true;  // true: rises to target, false: falls to it
};
/** The stored price alerts (any task: alerts.json is locked while read / written). */
size_t listPriceAlerts(const char* dir, std::vector<PriceAlertInfo>& out);
bool removePriceAlert(const char* dir, int id);
/** A new target, same direction. */
bool setPriceAlertTarget(const char* dir, int id, double target);

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
