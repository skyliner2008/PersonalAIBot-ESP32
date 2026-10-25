/**
 * @file CardScreen.h
 * @brief Full-screen info cards (weather, oil prices, market quotes, news) shown while the assistant speaks
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * The web-tools task fills a CardData and calls gCard.show(); the UI task pushes the card over the
 * face (only from the face, never over a menu) and closes it after ~30 s or on a tap.
 */

#pragma once

#include <math.h>

#include "UiManager.h"

struct CardData {
    enum Kind : uint8_t { None, Weather, Oil, Quotes, News, Text, Table, Analysis, Graph, Bars } kind = None;
    char title[72] = "";
    char subtitle[96] = "";
    // ---- text card: optional big line + wrapped paragraph ----
    char big[48] = "";
    char body[720] = "";
    // ---- analysis card: TradingView rating -1 (strong sell) .. +1 (strong buy), -9 = none ----
    float gauge = -9;

    // ---- weather ----
    struct Day {
        char label[16];  // "วันนี้", "ศ."
        int8_t code;     // WMO weather code
        int8_t pop;      // max precipitation probability %
        float tmax, tmin;
    };
    struct WeatherInfo {
        float temp = 0, feels = 0, wind = 0, rain = 0;
        int16_t humidity = 0, pm25 = -1, aqi = -1;
        int8_t code = 0;
        bool isDay = true;
        uint8_t days = 0;
        Day day[5];
    } w;

    // ---- graph: one line over time (a station's water level) with dashed reference lines (T100) ----
    struct Series {
        static constexpr int kMax = 60;
        uint8_t n = 0;
        float v[kMax];
        float ref[3] = {NAN, NAN, NAN};   // bank / warning / critical level (NAN = none)
        char from[17] = "", to[17] = "";  // "2026-10-24 00:00" .. "2026-10-25 22:30"
        char unit[16] = "";               // "ม.รทก."
        uint16_t color = 0;               // line colour (0 = accent)
    } g;

    // ---- rows: oil / quotes / news / bars (value, extra = value text, color) ----
    struct Row {
        char name[180];   // product / instrument / headline (~60 Thai characters)
        char extra[32];   // symbol, source, currency ...
        float value = 0;  // price
        float change = 0, changePct = 0;
        float next = 0;   // oil: tomorrow's price (0 = unknown)
        float high = 0, low = 0, open = 0;
        float rsi = -1, rating = -9;  // rating: TradingView Recommend.All (-1..1), -9 = none
        float perf[4] = {0, 0, 0, 0};  // 1W, 1M, 3M, 1Y %
        bool hasPerf = false;
        uint16_t color = 0;            // table / analysis: colour of `extra` (0 = default)
    };
    static constexpr int kMaxRows = 8;
    uint8_t rows = 0;
    Row row[kMaxRows];
};

class CardScreen : public Screen {
public:
    void begin();

    // ---- any task ----
    void show(const CardData& d, uint32_t holdMs = 30000);
    bool hasPending() const { return _pending; }

    // ---- UI task ----
    void activate();                    // take the pending card
    bool expired(uint32_t now) const { return (int32_t)(now - _closeAt) > 0; }

    void onTouch(const TouchEvent& e) override;
    void draw(LGFX_Sprite& c) override;

private:
    SemaphoreHandle_t _lock = nullptr;
    CardData _next;
    CardData _cur;
    volatile bool _pending = false;
    uint32_t _hold = 30000;
    uint32_t _closeAt = 0;

    void drawWeather(LGFX_Sprite& c);
    void drawOil(LGFX_Sprite& c);
    void drawQuote(LGFX_Sprite& c);
    void drawQuotes(LGFX_Sprite& c);
    void drawNews(LGFX_Sprite& c);
    void drawText(LGFX_Sprite& c);
    void drawTable(LGFX_Sprite& c);
    void drawAnalysis(LGFX_Sprite& c);
    void drawGraph(LGFX_Sprite& c);
    void drawBars(LGFX_Sprite& c);
    void drawHeader(LGFX_Sprite& c);
    /** Thai-aware wrap (syllables) with the sprite's metrics; returns the y after the last line. */
    int drawWrapped(LGFX_Sprite& c, const char* text, int x, int y, int w, int maxLines, int lineH, uint16_t color);
};

extern CardScreen gCard;

namespace wx {
/** WMO weather code -> Thai description. */
const char* describe(int code);
/** Draw a weather icon (sun / moon / clouds / rain / storm / fog / snow) centred at (cx, cy). */
void drawIcon(LGFX_Sprite& c, int cx, int cy, int size, int code, bool isDay);
}  // namespace wx
